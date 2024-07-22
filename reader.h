#ifndef CPP_ASYNC_HTTP_READER_H
#define CPP_ASYNC_HTTP_READER_H

#include <new>

#include "future.h"
#include "utils.h"

class Reader;

template <class Derived, typename Output>
class ReadFuture : public Future<Derived, Output> {
   public:
    Reader *getReader() { return static_cast<Derived *>(this)->getReader(); }
};

template <typename Output>
class VirtualReadFuture : public ReadFuture<VirtualReadFuture<Output>, Output> {
   public:
    virtual Reader *getReader() = 0;

    virtual Poll<Output> poll() = 0;
};

class ReadIntoBuffer : public VirtualReadFuture<size_t> {
    virtual char *getBuffer() = 0;

    virtual size_t getBufferLength() = 0;
};

typedef VirtualReadFuture<Optional<char>> Peek;

class Reader {
   public:
    virtual ReadIntoBuffer *readIntoBuffer(char *buffer,
                                           size_t bufferLength) = 0;

    virtual Peek *peek() = 0;
};

class ReadChar : public ReadFuture<ReadChar, Optional<char>> {
   public:
    explicit ReadChar(Reader *reader) : init({reader}) {}

    Reader *getReader() {
        switch (state) {
            case State::INIT:
                return init.reader;
            case State::AFTER_INIT:
                return afterInit.read->getReader();
        }
        return nullptr;
    }

    Poll<Optional<char>> poll() {
        switch (state) {
            case State::INIT: {
                state = State::AFTER_INIT;
                Reader *reader = init.reader;
                afterInit.read = reader->readIntoBuffer(&afterInit.c, 1);
            }
                // Fallthrough
            case State::AFTER_INIT:
                Poll<size_t> poll = afterInit.read->poll();
                if (poll.isReady()) {
                    if (poll.get() == 0) {
                        return Poll<Optional<char>>::ready(
                            Optional<char>::empty());
                    } else {
                        // poll must be at least 1
                        return Poll<Optional<char>>::ready(
                            Optional<char>::of(afterInit.c));
                    }
                }
                break;
        }
        return Poll<Optional<char>>::ready(Optional<char>::empty());
    }

   private:
    enum class State { INIT, AFTER_INIT } state = State::INIT;

    union {
        struct {
            Reader *reader;
        } init;
        struct {
            char c;
            ReadIntoBuffer *read;
        } afterInit;
    };
};

// A wrapper around a ReadImpl that provides a simple interface for
// reading/peeking data ReadImpl must have the function: Optional<size_t>
// readIntoBuffer(char *buffer, size_t bufferLength)
template <typename ReadImpl>
class SimpleReader : public Reader {
   public:
    explicit SimpleReader(ReadImpl impl) : impl(impl) {}

    SimpleReader(SimpleReader<ReadImpl> &other)
        : impl(other.impl),
          peekedChar(other.peekedChar),
          hasPeeked(other.hasPeeked),
          currentOpType(Op::NONE) {  // copy constructor
    }

    SimpleReader(SimpleReader<ReadImpl> &&other)
        : impl(other.impl),
          peekedChar(other.peekedChar),
          hasPeeked(other.hasPeeked),
          currentOpType(Op::NONE) {  // move constructor
    }

    SimpleReader<ReadImpl> &operator=(
        SimpleReader<ReadImpl> &other) {  // copy assignment
        copyFrom(other);
        return *this;
    }

    SimpleReader &operator=(
        SimpleReader<ReadImpl> &&other) {  // move assignment
        copyFrom(other);
        return *this;
    }

    ~SimpleReader() {  // destructor
        destructPrevOp();
    }

    ReadImpl *getImpl() { return &impl; }

    ReadIntoBuffer *readIntoBuffer(char *buffer, size_t bufferLength) override {
        destructPrevOp();

        currentOpType = Op::READ;
        void *ptr = (void *)&currentOp;
        return (ReadIntoBuffer *)new (ptr)
            ReadIntoBufferImpl(this, buffer, bufferLength);
    }

    Peek *peek() override {
        destructPrevOp();

        currentOpType = Op::PEEK;
        void *ptr = (void *)&currentOp;
        return (Peek *)new (ptr) PeekImpl(this);
    }

   private:
    void copyFrom(SimpleReader<ReadImpl> &other) {
        this->impl = other.impl;
        this->peekedChar = other.peekedChar;
        this->hasPeeked = other.hasPeeked;
        this->currentOpType = Op::NONE;
    }

    char peekedChar;
    bool hasPeeked = false;

    class ReadIntoBufferImpl : ReadIntoBuffer {
       public:
        explicit ReadIntoBufferImpl(SimpleReader<ReadImpl> *reader,
                                    char *buffer, size_t bufferLength)
            : reader(reader), buffer(buffer), bufferLength(bufferLength) {}

        SimpleReader<ReadImpl> *getReader() override { return reader; }

        char *getBuffer() override { return buffer; }

        size_t getBufferLength() override { return bufferLength; }

        Poll<size_t> poll() override {
            switch (reader->hasPeeked) {
                case true:
                    reader->hasPeeked = false;
                    buffer[0] = reader->peekedChar;
                    offset++;
                    // Fallthrough
                case false:
                    Optional<size_t> opt = reader->impl.readIntoBuffer(
                        buffer + offset, bufferLength - offset);
                    if (opt.isPresent()) {
                        size_t read = opt.get();
                        offset += read;
                    } else {
                        return Poll<size_t>::ready(offset);
                    }
                    break;
            }

            if (offset == bufferLength) {
                return Poll<size_t>::ready(offset);
            }

            return Poll<size_t>::pending();
        }

       private:
        SimpleReader *reader;
        char *buffer;
        size_t bufferLength;
        size_t offset = 0;
    };

    class PeekImpl : Peek {
       public:
        explicit PeekImpl(SimpleReader<ReadImpl> *reader) : reader(reader) {}

        SimpleReader<ReadImpl> *getReader() override { return reader; }

        Poll<Optional<char>> poll() override {
            if (!reader->hasPeeked) {
                Optional<size_t> opt =
                    reader->impl.readIntoBuffer(&reader->peekedChar, 1);
                if (opt.isPresent()) {
                    size_t read = opt.get();
                    if (read == 0) {
                        return Poll<Optional<char>>::pending();
                    }

                    reader->hasPeeked = true;
                    return Poll<Optional<char>>::ready(
                        Optional<char>::of(reader->peekedChar));
                } else {
                    return Poll<Optional<char>>::ready(Optional<char>::empty());
                }
            } else {
                return Poll<Optional<char>>::ready(
                    Optional<char>::of(reader->peekedChar));
            }
        }

       private:
        SimpleReader *reader;
    };

    enum class Op { NONE, READ, PEEK } currentOpType = Op::NONE;
    char currentOp[max(sizeof(ReadIntoBufferImpl), sizeof(PeekImpl))];

    void destructPrevOp() {
        void *currentOp = this->currentOp;
        switch (currentOpType) {
            case Op::READ:
                ((ReadIntoBufferImpl *)currentOp)->~ReadIntoBufferImpl();
                break;
            case Op::PEEK:
                ((PeekImpl *)currentOp)->~PeekImpl();
                break;
        }
        currentOpType = Op::NONE;
    }

    ReadImpl impl;
};

// F needs to overload: bool operator()(char c);
template <typename F>
class ReadWhile : public ReadFuture<ReadWhile<F>, void_> {
   public:
    explicit ReadWhile(Reader *reader, F f) : init({reader}), f(f) {}

    Reader *getReader() {
        switch (state) {
            case State::INIT:
                return init.reader;
            case State::PEEK:
                return peek->getReader();
            case State::READ:
                return read.getReader();
        }
        return nullptr;
    }

    F *getFunc() { return &f; }

    Poll<void_> poll() {
        switch (state) {
            case State::INIT:
                state = State::PEEK;
                peek = init.reader->peek();
                // Fallthrough
            default:
                return peekAndRead();
        }
    }

   private:
    Poll<void_> peekAndRead() {
        while (true) {
            switch (state) {
                case State::PEEK:
                peek: {
                    Poll<Optional<char>> poll = peek->poll();
                    if (poll.isPending()) {
                        return Poll<void_>::pending();
                    }

                    Optional<char> opt = poll.get();
                    if (opt.isPresent()) {
                        char c = opt.get();
                        if (f(c)) {
                            state = State::READ;

                            Reader *reader = peek->getReader();
                            read = ReadChar(reader);
                            // Only this case will fallthrough. the others will
                            // return
                        } else {
                            return Poll<void_>::ready(void_());
                        }
                    } else {
                        READY(void_())
                    }
                }
                    // Fallthrough
                case State::READ: {
                    Poll<Optional<char>> poll = read.poll();
                    if (poll.isPending()) {
                        return Poll<void_>::pending();
                    }

                    // We don't care about the result of the read

                    state = State::PEEK;

                    Reader *reader = read.getReader();
                    peek = reader->peek();

                    goto peek;
                } break;
            }
        }
    }

    enum class State {
        INIT,
        PEEK,
        READ,
    } state = State::INIT;

    union {
        struct {
            Reader *reader;
        } init;
        Peek *peek;
        ReadChar read;
    };
    F f;
};

// Store must have: bool push(char);
// F must overload: bool operator()(char);
// Returns if store had enough space(push always returned true = this will
// return true)
template <typename Store, typename F>
class ReadIntoStoreWhile
    : public ReadFuture<ReadIntoStoreWhile<Store, F>, bool> {
   public:
    ReadIntoStoreWhile(Reader *reader, Store *store, F f)
        : init{reader, store, f} {}

    Reader *getReader() {
        switch (state) {
            case State::INIT:
                return init.reader;
            case State::READ_WHILE:
                return readWhile.getReader();
        }
        return nullptr;
    }

    Store *getStore() {
        switch (state) {
            case State::INIT:
                return init.store;
            case State::READ_WHILE:
                return readWhile.getFunc()->store;
        }
        return nullptr;
    }

    F *getFunc() {
        switch (state) {
            case State::INIT:
                return &init.f;
            case State::READ_WHILE:
                return readWhile.getFunc()->f;
        }
        return nullptr;
    }

    Poll<bool> poll() {
        switch (state) {
            case State::INIT: {
                Reader *reader = init.reader;
                Store *store = init.store;
                F f = init.f;
                auto future = ReadWhile<Read>(reader, Read(store, f));
                INIT_AWAIT(READ_WHILE, readWhile, future, result)

                READY(readWhile.getFunc()->success)
            }
        }
        return Poll<bool>::pending();
    }

   private:
    enum class State { INIT, READ_WHILE } state = State::INIT;
    struct Read {
        Read(Store *store, F f) : store(store), f(f) {}

        bool success = true;
        Store *store;
        F f;

        bool operator()(char c) {
            if (!f(c)) {
                return false;
            }
            if (!store->push(c)) {
                success = false;
                return false;
            }
            return true;
        }
    };

    union {
        struct {
            Reader *reader;
            Store *store;
            F f;
        } init;
        ReadWhile<Read> readWhile;
    };
};

// F must overload: bool operator()(char);
template <typename F>
class ReadIntoWhile : public ReadFuture<ReadIntoWhile<F>, size_t> {
   public:
    explicit ReadIntoWhile(Reader *reader, char *buffer, size_t length, F f)
        : init({reader, {buffer, 0, length, f}}) {}

    Reader *getReader() { return readWhile.getReader(); }

    F *getFunc() {
        switch (state) {
            case State::INIT: {
                return init.read;
            }
            case State::READ_WHILE: {
                return readWhile.getFunc();
            }
        }
    }

    char *getBuffer() { return readWhile.getFunc()->buffer; }

    size_t getBufferLength() { return readWhile.getFunc()->bufferLength; }

    Poll<size_t> poll() {
        switch (state) {
            case State::INIT: {
                state = State::READ_WHILE;
                readWhile = ReadWhile<Read>(init.reader, init.read);
            }
                // Fallthrough
            case State::READ_WHILE: {
                Poll<void_> poll = readWhile.poll();
                if (poll.isPending()) {
                    return Poll<size_t>::pending();
                }
                return Poll<size_t>::ready(readWhile.getFunc()->read);
            }
        }

        return Poll<size_t>::pending();
    }

   private:
    struct Read {
        char *buffer;
        size_t read;
        size_t bufferLength;
        F f;

        bool operator()(char c) {
            bool continu = f(c);
            if (continu) {
                if (read >= bufferLength) {
                    return false;
                }
                buffer[read] = c;
                read++;
            }
            return continu;
        }
    };

    enum class State {
        INIT,
        READ_WHILE,
    } state = State::INIT;

    union {
        struct {
            Reader *reader;
            Read read;
        } init;
        ReadWhile<Read> readWhile;
    };
};

// Returns empty if the crlf is invalid or the inReader doesn't have enough
// data. If found returns true, if not found false(no data consumed).
class ReadCrlf : public ReadFuture<ReadCrlf, Optional<bool>> {
   public:
    explicit ReadCrlf(Reader *reader) : init({reader}) {}

    Reader *getReader() {
        switch (state) {
            case State::INIT:
                return init.reader;
            case State::PEEK:
                return peek->getReader();
            case State::READ_CR:
                return read.getReader();
            case State::READ_LF:
                return read.getReader();
        }
        return nullptr;
    }

    Poll<Optional<bool>> poll() {
        switch (state) {
            case State::INIT:
                state = State::PEEK;
                peek = init.reader->peek();
                // Fallthrough
            case State::PEEK: {
                Poll<Optional<char>> poll = peek->poll();
                if (poll.isPending()) {
                    return Poll<Optional<bool>>::pending();
                }

                Optional<char> opt = poll.get();
                if (!opt.isPresent()) {
                    return Poll<Optional<bool>>::ready(Optional<bool>::empty());
                }

                char c = opt.get();
                if (c != '\r') {
                    return Poll<Optional<bool>>::ready(
                        Optional<bool>::of(false));
                }

                state = State::READ_CR;

                Reader *reader = peek->getReader();
                read = ReadChar(reader);
            }
                // Fallthrough
            case State::READ_CR: {
                Poll<Optional<char>> poll = read.poll();
                if (poll.isPending()) {
                    return Poll<Optional<bool>>::pending();
                }

                Optional<char> opt = poll.get();
                if (!opt.isPresent()) {
                    return Poll<Optional<bool>>::ready(Optional<bool>::empty());
                }
                // We know it's a \r

                state = State::READ_LF;

                Reader *reader = read.getReader();
                read = ReadChar(reader);
            }
                // Fallthrough
            case State::READ_LF: {
                Poll<Optional<char>> poll = read.poll();
                if (poll.isPending()) {
                    return Poll<Optional<bool>>::pending();
                }

                Optional<char> opt = poll.get();
                if (!opt.isPresent()) {
                    return Poll<Optional<bool>>::ready(Optional<bool>::empty());
                }

                char c = opt.get();
                if (c != '\n') {
                    return Poll<Optional<bool>>::ready(
                        Optional<bool>::of(false));
                }

                return Poll<Optional<bool>>::ready(Optional<bool>::of(true));
            }
        }

        return Poll<Optional<bool>>::pending();
    }

   private:
    enum class State { INIT, PEEK, READ_CR, READ_LF } state = State::INIT;

    union {
        struct {
            Reader *reader;
        } init;
        Peek *peek;
        ReadChar read;
    };
};

class ReadNumber : public ReadFuture<ReadNumber, Optional<double>> {
   public:
    ReadNumber(Reader *reader) : init({reader}) {}

    Reader *getReader() {
        switch (state) {
            case State::INIT:
                return init.reader;
            case State::PEEK_DECIMAL:
            case State::PEEK_FRACTION:
                return peek->getReader();
            case State::READ_DECIMAL:
            case State::READ_FRACTION:
                return readChar.getReader();
        }
        return nullptr;
    }

    Poll<Optional<double>> poll() {
        switch (state) {
            case State::INIT: {
                state = State::PEEK_DECIMAL;

                Reader *reader = init.reader;
                peek = reader->peek();
            }
            case State::PEEK_DECIMAL:
            peekDecimal: {
                Poll<Optional<char>> poll = peek->poll();
                if (poll.isPending()) {
                    return Poll<Optional<double>>::pending();
                }

                Optional<char> opt = poll.get();
                if (opt.isEmpty()) {
                    return Poll<Optional<double>>::ready(
                        Optional<double>::of(getNumber()));
                }

                char c = opt.get();
                if (!((c >= '0' && c <= '9') || c == '-' || c == '.')) {
                    if (!hasValue) {
                        return Poll<Optional<double>>::ready(
                            Optional<double>::empty());
                    } else {
                        return Poll<Optional<double>>::ready(
                            Optional<double>::of(getNumber()));
                    }
                }

                state = State::READ_DECIMAL;

                Reader *reader = peek->getReader();
                readChar = ReadChar(reader);
            }
                // Fallthrough
            case State::READ_DECIMAL: {
                Poll<Optional<char>> poll = readChar.poll();
                if (poll.isPending()) {
                    return Poll<Optional<double>>::pending();
                }

                Optional<char> opt = poll.get();
                if (opt.isEmpty()) {
                    return Poll<Optional<double>>::ready(
                        Optional<double>::empty());
                }

                char c = opt.get();
                if (c >= '0' && c <= '9') {
                    hasValue = true;

                    int digit = c - '0';
                    number = number * 10 + digit;

                    state = State::PEEK_DECIMAL;

                    Reader *reader = readChar.getReader();
                    peek = reader->peek();
                    goto peekDecimal;
                } else if (c == '-') {
                    if (number != 0) {
                        return Poll<Optional<double>>::ready(
                            Optional<double>::of(getNumber()));
                    }
                    negative = true;

                    state = State::PEEK_DECIMAL;

                    Reader *reader = readChar.getReader();
                    peek = reader->peek();
                    goto peekDecimal;
                } else if (c == '.') {
                    hasValue = true;

                    state = State::PEEK_FRACTION;

                    Reader *reader = readChar.getReader();
                    peek = reader->peek();
                    // Fallthrough
                } else {
                    if (!hasValue) {
                        return Poll<Optional<double>>::ready(
                            Optional<double>::empty());
                    }
                    return Poll<Optional<double>>::ready(
                        Optional<double>::of(getNumber()));
                }
            }
            case State::PEEK_FRACTION:
            peekFraction: {
                Poll<Optional<char>> poll = peek->poll();
                if (poll.isPending()) {
                    return Poll<Optional<double>>::pending();
                }

                Optional<char> opt = poll.get();
                if (opt.isEmpty()) {
                    return Poll<Optional<double>>::ready(
                        Optional<double>::of(getNumber()));
                }

                char c = opt.get();
                if (!(c >= '0' && c <= '9')) {
                    return Poll<Optional<double>>::ready(
                        Optional<double>::of(getNumber()));
                }

                state = State::READ_FRACTION;

                Reader *reader = peek->getReader();
                readChar = ReadChar(reader);
            }
                // Fallthrough
            case State::READ_FRACTION: {
                Poll<Optional<char>> poll = readChar.poll();
                if (poll.isPending()) {
                    return Poll<Optional<double>>::pending();
                }

                Optional<char> opt = poll.get();
                if (opt.isEmpty()) {
                    return Poll<Optional<double>>::ready(
                        Optional<double>::empty());
                }

                char c = opt.get();
                if (c >= '0' && c <= '9') {
                    int digit = c - '0';
                    number += fractionMultiply * digit;
                    fractionMultiply /= 10;

                    state = State::PEEK_FRACTION;

                    Reader *reader = readChar.getReader();
                    peek = reader->peek();
                    goto peekFraction;
                } else {
                    return Poll<Optional<double>>::ready(
                        Optional<double>::of(getNumber()));
                }
            }
        }

        return Poll<Optional<double>>::pending();
    }

   private:
    double getNumber() {
        if (negative) {
            return -number;
        }
        return number;
    }

    enum class State {
        INIT,
        PEEK_DECIMAL,
        READ_DECIMAL,
        PEEK_FRACTION,
        READ_FRACTION,
    } state = State::INIT;

    bool hasValue = false;
    bool negative = false;
    double number = 0;
    double fractionMultiply = 0.1;

    union {
        struct {
            Reader *reader;
        } init;
        Peek *peek;
        ReadChar readChar;
    };
};

#endif
