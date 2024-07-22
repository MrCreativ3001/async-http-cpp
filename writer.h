#ifndef CPP_ASYNC_HTTP_WRITER_H
#define CPP_ASYNC_HTTP_WRITER_H

#include <new>

#include "future.h"
#include "utils.h"

class Writer;

// TODO: remove virtual functions and use templates instead!

template <typename Derived, typename Output>
class WriteFuture : public Future<Derived, Output> {
   public:
    Writer *getWriter() { return static_cast<Derived *>(this)->getWriter(); }
};

template <typename Output>
class VirtualWriteFuture
    : public WriteFuture<VirtualWriteFuture<Output>, Output> {
   public:
    virtual Writer *getWriter() = 0;

    virtual Poll<Output> poll() = 0;
};

class WriteFromBuffer : public VirtualWriteFuture<size_t> {
   public:
    virtual const char *getBuffer() = 0;

    virtual size_t getBufferLength() = 0;
};

class Writer {
   public:
    virtual WriteFromBuffer *writeFromBuffer(const char *buffer,
                                             size_t bufferLength) = 0;
};

// WriteImpl must have the function Optional<size_t> writeFromBuffer(const char*
// buffer, size_t bufferLength) where an empty optional means that the writer is
// full.
template <typename WriteImpl>
class SimpleWriter : public Writer {
   public:
    explicit SimpleWriter(WriteImpl impl) : impl(impl) {}

    SimpleWriter(SimpleWriter<WriteImpl> &other)
        : impl(other.impl), isWriting(false) {  // copy constructor
    }

    SimpleWriter(SimpleWriter<WriteImpl> &&other)
        : impl(other.impl), isWriting(false) {  // move constructor
    }

    SimpleWriter<WriteImpl> &operator=(
        SimpleWriter<WriteImpl> &other) {  // copy assignment
        copyFrom(other);
        return *this;
    }

    SimpleWriter<WriteImpl> &operator=(
        SimpleWriter<WriteImpl> &&other) {  // move assignment
        copyFrom(other);
        return *this;
    }

    ~SimpleWriter() { destructOp(); }

    WriteImpl *getImpl() { return &impl; }

    WriteFromBuffer *writeFromBuffer(const char *buffer,
                                     size_t length) override {
        isWriting = true;

        void *ptr = (void *)&writeOp;
        return (WriteFromBuffer *)new (ptr)
            WriteFromBufferImpl(this, buffer, length);
    }

   private:
    void copyFrom(SimpleWriter<WriteImpl> &other) {
        impl = other.impl;
        isWriting = false;
    }

    WriteImpl impl;

    class WriteFromBufferImpl : public WriteFromBuffer {
       public:
        WriteFromBufferImpl(SimpleWriter<WriteImpl> *writer, const char *buffer,
                            size_t length)
            : writer(writer), buffer(buffer), length(length) {}

        SimpleWriter<WriteImpl> *getWriter() override { return writer; }

        const char *getBuffer() override { return buffer; }

        size_t getBufferLength() override { return length; }

        Poll<size_t> poll() override {
            Optional<size_t> write = writer->impl.writeFromBuffer(
                buffer + written, length - written);
            if (!write.isPresent()) {
                return Poll<size_t>::ready(written);
            }

            written += write.get();
            if (written == length) {
                return Poll<size_t>::ready(written);
            }

            return Poll<size_t>::pending();
        }

       private:
        SimpleWriter<WriteImpl> *writer;
        const char *buffer;
        size_t length;
        size_t written = 0;
    };

    void destructOp() {
        if (isWriting) {
            ((WriteFromBufferImpl *)&writeOp)->~WriteFromBufferImpl();
            isWriting = false;
        }
    }

    bool isWriting = false;
    char writeOp[sizeof(WriteFromBufferImpl)];
};

class WriteChar : public WriteFuture<WriteChar, bool> {
   public:
    WriteChar(Writer *writer, char c) : init({writer}), c(c) {}

    Writer *getWriter() {
        switch (state) {
            case State::INIT:
                return init.writer;
            case State::WRITE:
                return write->getWriter();
        }
        return nullptr;
    }

    Poll<bool> poll() {
        switch (state) {
            case State::INIT:
                state = State::WRITE;

                write = init.writer->writeFromBuffer(&c, 1);
            case State::WRITE: {
                Poll<size_t> poll = write->poll();
                if (poll.isPending()) {
                    return Poll<bool>::pending();
                }

                bool success = poll.get() == 1;

                return Poll<bool>::ready(success);
            }
        }

        return Poll<bool>::pending();
    }

   private:
    enum class State { INIT, WRITE } state = State::INIT;
    char c;
    union {
        struct {
            Writer *writer;
        } init;
        WriteFromBuffer *write;
    };
};

class WriteCrlf {
   public:
    explicit WriteCrlf(Writer *writer) : init({writer}) {}

    Writer *getWriter() {
        switch (state) {
            case State::INIT:
                return init.writer;
            default:
                return writeChar.getWriter();
        }
    }

    Poll<bool> poll() {
        switch (state) {
            case State::INIT: {
                state = State::CR;

                Writer *writer = init.writer;
                writeChar = WriteChar(writer, '\r');
            }
                // Fallthrough
            case State::CR: {
                Poll<bool> poll = writeChar.poll();
                if (poll.isPending()) {
                    return Poll<bool>::pending();
                }

                if (!poll.get()) {
                    return Poll<bool>::ready(false);
                }

                state = State::LF;

                Writer *writer = writeChar.getWriter();
                writeChar = WriteChar(writer, '\n');
            }
                // Fallthrough
            case State::LF: {
                Poll<bool> poll = writeChar.poll();
                if (poll.isPending()) {
                    return Poll<bool>::pending();
                }

                bool success = poll.get();

                return Poll<bool>::ready(success);
            }
        }

        return Poll<bool>::pending();
    }

   private:
    enum class State { INIT, CR, LF } state = State::INIT;

    union {
        struct {
            Writer *writer;
        } init;
        WriteChar writeChar;
    };
};

template <int Base>
class WriteDouble : public WriteFuture<WriteDouble<10>, bool> {
   public:
    WriteDouble(Writer *writer, double number)
        : init({writer}), number(number) {}

    Writer *getWriter() {
        switch (state) {
            case State::INIT:
                return init.writer;
            case State::WRITE_NEGATIVE:
            case State::WRITE_DECIMAL:
            case State::WRITE_FRACTION:
            case State::WRITE_ZERO:
                return writeChar.getWriter();
        }
        return nullptr;
    }

    Poll<bool> poll() {
        switch (state) {
            case State::INIT: {
                Writer *writer = init.writer;
                if (number == 0) {
                    state = State::WRITE_ZERO;

                    writeChar = WriteChar(writer, '0');
                    goto writeZero;
                }

                double number = this->number;
                numberInfo = createNumberInfo(number);

                if (!numberInfo.negative) {
                    goto prepareWriteDecimal;
                }

                state = State::WRITE_NEGATIVE;

                writeChar = WriteChar(writer, '-');
            }
            case State::WRITE_NEGATIVE:
            writeNegative : {
                Poll<bool> poll = writeChar.poll();
                if (poll.isPending()) {
                    return Poll<bool>::pending();
                }

                if (!poll.get()) {
                    return Poll<bool>::ready(false);
                }
            }
                // Fallthrough
            prepareWriteDecimal : {
                if (numberInfo.decimalPoint == 0) {
                    goto prepareWriteFraction;
                }

                int digit = numberInfo.reversedNumber % Base;
                numberInfo.reversedNumber /= Base;
                numberInfo.decimalPoint--;

                Writer *writer = getWriter();

                state = State::WRITE_DECIMAL;

                writeChar = WriteChar(writer, getDigit(digit));
            }
            case State::WRITE_DECIMAL: {
                Poll<bool> poll = writeChar.poll();
                if (poll.isPending()) {
                    return Poll<bool>::pending();
                }

                if (!poll.get()) {
                    return Poll<bool>::ready(false);
                }

                goto prepareWriteDecimal;
            }
            prepareWriteFraction : {
                if (numberInfo.reversedNumber == 1) {
                    return Poll<bool>::ready(true);
                }
                if (numberInfo.decimalPoint == 0) {
                    numberInfo.decimalPoint--;
                    Writer *writer = getWriter();

                    state = State::WRITE_FRACTION;

                    writeChar = WriteChar(writer, '.');
                    goto writeFraction;
                }

                int digit = numberInfo.reversedNumber % Base;
                numberInfo.reversedNumber /= Base;

                Writer *writer = getWriter();

                state = State::WRITE_FRACTION;

                writeChar = WriteChar(writer, getDigit(digit));
            }
            case State::WRITE_FRACTION:
            writeFraction : {
                Poll<bool> poll = writeChar.poll();
                if (poll.isPending()) {
                    return Poll<bool>::pending();
                }

                if (!poll.get()) {
                    return Poll<bool>::ready(false);
                }

                goto prepareWriteFraction;
            }
            case State::WRITE_ZERO:
            writeZero : {
                Poll<bool> poll = writeChar.poll();
                if (poll.isPending()) {
                    return Poll<bool>::pending();
                }

                if (!poll.get()) {
                    return Poll<bool>::ready(false);
                }

                return Poll<bool>::ready(true);
            }
        }

        return Poll<bool>::pending();
    }

   private:
    static_assert(Base >= 2 && Base <= 45, "Base must be between 2 and 45");

    enum class State {
        INIT,
        WRITE_NEGATIVE,
        WRITE_DECIMAL,
        WRITE_FRACTION,
        WRITE_ZERO,
    } state = State::INIT;

    struct NumberInfo {
        bool negative;
        unsigned long reversedNumber;
        char decimalPoint;
    };

    // ex: 123.456 -> 1654321.(in base 10) There will always be a one at the end
    // of the number so that zeros are not lost.
    static NumberInfo createNumberInfo(double number) {
        NumberInfo info;
        info.negative = number < 0;
        if (info.negative) {
            number = -number;
        }

        // before decimal point
        double tmp = number;
        info.reversedNumber = 1;
        info.decimalPoint = 0;
        while (tmp >= 1) {
            char digit = (int)tmp % Base;
            tmp /= Base;
            info.reversedNumber *= Base;
            info.reversedNumber += digit;

            info.decimalPoint++;
        }

        // after decimal point
        // TODO

        return info;
    }

    static char getDigit(int digit) {
        return digit < 10 ? '0' + digit : 'a' + digit - 10;
    }

    union {
        double number;
        NumberInfo numberInfo;
    };

    union {
        struct {
            Writer *writer;
        } init;
        WriteChar writeChar;
    };
};

#endif
