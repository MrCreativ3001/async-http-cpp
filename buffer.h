#ifndef CPP_ASYNC_HTTP_BUFFER_H
#define CPP_ASYNC_HTTP_BUFFER_H

#include "reader.h"
#include "utils.h"
#include "writer.h"

class BufferReadImpl {
   public:
    explicit BufferReadImpl(BufferRef buffer) : buffer(buffer), offset(0) {}

    Optional<size_t> readIntoBuffer(char *buffer, size_t bufferLength) {
        if (offset >= this->buffer.length) {
            return Optional<size_t>::empty();
        }
        size_t available = this->buffer.length - offset;
        size_t read = min(available, bufferLength);

        memcpy(buffer, this->buffer.data + offset, read);

        offset += read;
        return Optional<size_t>::of(read);
    }

   private:
    BufferRef buffer;
    size_t offset;
};

typedef SimpleReader<BufferReadImpl> BufferReader;

BufferReader readFromBuffer(BufferRef buffer) {
    return BufferReader(BufferReadImpl(buffer));
}

Optional<double> readDoubleFromBuffer(BufferRef buffer) {
    BufferReader reader = readFromBuffer(buffer);
    return blockOn(ReadNumber(&reader));
}

class BufferWriteImpl {
   public:
    explicit BufferWriteImpl(BufferRef buffer) : buffer(buffer), offset(0) {}

    Optional<size_t> writeFromBuffer(const char *buffer, size_t bufferLength) {
        if (offset >= this->buffer.length) {
            return Optional<size_t>::empty();
        }
        size_t available = this->buffer.length - offset;
        size_t write = min(available, bufferLength);

        memcpy(this->buffer.data + offset, buffer, write);

        offset += write;
        return Optional<size_t>::of(write);
    }

    BufferRef getFilledBuffer() { return buffer.asRef(offset); }

   private:
    BufferRef buffer;
    size_t offset;
};

typedef SimpleWriter<BufferWriteImpl> BufferWriter;

BufferWriter writeToBuffer(BufferRef buffer) {
    return BufferWriter(BufferWriteImpl(buffer));
}

template <int Base, size_t BufferLength>
Optional<SizedBuffer<BufferLength>> writeDoubleToBuffer(double value) {
    SizedBuffer<BufferLength> buffer = SizedBuffer<BufferLength>();
    BufferWriter writer = writeToBuffer(buffer.asFullRef());

    bool result = blockOn(WriteDouble<Base>(&writer, value));
    if (!result) {
        return Optional<SizedBuffer<BufferLength>>::empty();
    }

    buffer.length = writer.getImpl()->getFilledBuffer().length;

    return Optional<SizedBuffer<BufferLength>>::of(buffer);
}

#endif
