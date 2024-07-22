#ifndef CPP_ASYNC_HTTP_JSON_H
#define CPP_ASYNC_HTTP_JSON_H

#include "future.h"
#include "reader.h"
#include "utils.h"

// helpful links
// json: https://www.json.org/json-en.html
// Parsing/Interpreting data: https://lisperator.net/pltut/parser/

namespace JsonConsts {
const char *NULL_STR = "null";
const char *TRUE = "true";
const char *FALSE = "false";
}  // namespace JsonConsts

#include "deser.h"

class ReadJsonNull : public ReadFuture<ReadJsonNull, bool> {
   public:
    // The buffer needs to be at LEAST 4 BYTES long!
    explicit ReadJsonNull(Reader *reader, char *buffer)
        : init({reader, buffer}) {}

    Poll<bool> poll() {
        switch (state) {
            case State::INIT: {
                state = State::READ;

                Reader *reader = init.reader;
                char *buffer = init.buffer;
                readIntoWhile = ReadIntoWhile<bool (*)(char)>(reader, buffer, 4,
                                                              isCharJsonNull);
            }
                // Fallthrough
            case State::READ: {
                Poll<size_t> poll = readIntoWhile.poll();
                if (poll.isPending()) {
                    return Poll<bool>::pending();
                }

                BufferRef bufferRef =
                    BufferRef(readIntoWhile.getBuffer(), poll.get());
                if (bufferRef == JsonConsts::NULL_STR) {
                    return Poll<bool>::ready(true);
                } else {
                    return Poll<bool>::ready(false);
                }
            }
        }

        return Poll<bool>::pending();
    }

   private:
    enum class State {
        INIT,
        READ,
    } state = State::INIT;

    union {
        struct {
            Reader *reader;
            char *buffer;
        } init;
        ReadIntoWhile<bool (*)(char)> readIntoWhile;
    };

    static bool isCharJsonNull(char c) {
        return c == 'n' || c == 'u' || c == 'l';
    }
};

class ReadJsonBoolean : public ReadFuture<ReadJsonBoolean, Optional<bool>> {
   public:
    // The buffer needs to be at LEAST 5 BYTES long!
    explicit ReadJsonBoolean(Reader *reader, char *buffer)
        : init({reader, buffer}) {}

    Poll<Optional<bool>> poll() {
        switch (state) {
            case State::INIT: {
                state = State::READ;

                Reader *reader = init.reader;
                char *buffer = init.buffer;
                readIntoWhile = ReadIntoWhile<bool (*)(char)>(
                    reader, buffer, 5, isCharJsonBoolean);
            }
                // Fallthrough
            case State::READ: {
                Poll<size_t> poll = readIntoWhile.poll();
                if (poll.isPending()) {
                    return Poll<Optional<bool>>::pending();
                }

                BufferRef bufferRef =
                    BufferRef(readIntoWhile.getBuffer(), poll.get());
                if (bufferRef == JsonConsts::TRUE) {
                    return Poll<Optional<bool>>::ready(
                        Optional<bool>::of(true));
                } else if (bufferRef == JsonConsts::FALSE) {
                    return Poll<Optional<bool>>::ready(
                        Optional<bool>::of(false));
                } else {
                    return Poll<Optional<bool>>::ready(Optional<bool>::empty());
                }
            }
        }

        return Poll<Optional<bool>>::pending();
    }

   private:
    enum class State {
        INIT,
        READ,
    } state = State::INIT;

    union {
        struct {
            Reader *reader;
            char *buffer;
        } init;
        ReadIntoWhile<bool (*)(char)> readIntoWhile;
    };

    static bool isCharJsonBoolean(char c) {
        return c == 't' || c == 'r' || c == 'u' || c == 'e' || c == 'f' ||
               c == 'a' || c == 'l' || c == 's';
    }
};

// F must overload: bool operator()(char);
template <typename F>
class ReadJsonString : public ReadFuture<ReadJsonString<F>, bool> {
   public:
    ReadJsonString(Reader *reader, F f) : init({reader}), func(f) {}

    F *getFunc() { return &func; }

    Poll<bool> poll() {
        switch (state) {
            case State::INIT:
                state = State::START_QUOTE;

                readChar = ReadChar(init.reader);
                // Fallthrough
            case State::START_QUOTE: {
                Poll<Optional<char>> poll = readChar.poll();
                if (poll.isPending()) {
                    return Poll<bool>::pending();
                }

                Optional<char> opt = poll.get();
                if (opt.isEmpty()) {
                    return Poll<bool>::ready(false);
                }

                char c = opt.get();
                if (c != '"') {
                    return Poll<bool>::ready(false);
                }

                state = State::READ_STRING;

                Reader *reader = readChar.getReader();

                readWhileString = ReadWhile<ReadWhileString>(
                    reader, ReadWhileString{false, &this->func});
            }
                // Fallthrough
            case State::READ_STRING: {
                Poll<void_> poll = readWhileString.poll();
                if (poll.isPending()) {
                    return Poll<bool>::pending();
                }

                state = State::END_QUOTE;

                Reader *reader = readWhileString.getReader();
                readChar = ReadChar(reader);
            }
                // Fallthrough
            case State::END_QUOTE: {
                Poll<Optional<char>> poll = readChar.poll();
                if (poll.isPending()) {
                    return Poll<bool>::pending();
                }

                Optional<char> opt = poll.get();
                if (opt.isEmpty()) {
                    return Poll<bool>::ready(false);
                }

                char c = opt.get();
                if (c != '"') {
                    return Poll<bool>::ready(false);
                }

                return Poll<bool>::ready(true);
            }
        }

        return Poll<bool>::pending();
    }

   private:
    enum class State {
        INIT,
        START_QUOTE,
        READ_STRING,
        END_QUOTE,
    } state = State::INIT;

    struct ReadWhileString {
        bool escaped;
        F *f;

        bool operator()(char c) {
            if (escaped) {
                escaped = false;
                return true;
            } else if (c == '\\') {
                escaped = true;
                return true;
            } else if (c == '"') {
                return false;
            } else {
                return (*f)(c);
            }
            // Should never happen
            return false;
        }
    };

    F func;
    union {
        struct {
            Reader *reader;
        } init;
        ReadWhile<ReadWhileString> readWhileString;
        ReadChar readChar;
    };
};

class JsonDeserializer {
   public:
    explicit JsonDeserializer(Reader *reader) : reader(reader) {}

    Reader *getReader() { return reader; }

    template <typename NameStore, typename StructVisitor>
    class DeserializeStructFuture
        : Future<DeserializeStructFuture<NameStore, StructVisitor>, bool> {
       private:
        struct ReadIntoNameStore {
            NameStore *nameStore;
            bool operator()(char c) { return nameStore->push(c); }
        };

       public:
        DeserializeStructFuture(JsonDeserializer *deserializer,
                                NameStore nameStore, StructVisitor visitor)
            : deserializer(deserializer),
              nameStore(nameStore),
              visitor(visitor) {}

        Poll<bool> poll() {
            switch (state) {
                case State::INIT: {
                    auto future = ReadWhile<bool (*)(char)>(
                        deserializer->reader, isCharWhitespace);
                    INIT_AWAIT(READ_WHITESPACES, readWhile, future, result)
                    // Result is of value void_

                    INIT_AWAIT(READ_OPEN_BRACKET, readChar,
                               ReadChar(deserializer->reader), result)
                    if (result.isEmpty()) {
                        READY(false)
                    }
                    char c = result.get();

                    if (c != '{') {
                        READY(false)
                    }
                }
                initPeekWhitespaces : {
                    state = State::READ_BEFORE_PEEK_WHITESPACES;
                    this->readWhile = ReadWhile<bool (*)(char)>(
                        deserializer->reader, isCharWhitespace);
                }
                case State::READ_BEFORE_PEEK_WHITESPACES: {
                    AWAIT(readWhile, result)
                    // result is void_

                    state = State::PEEK;
                    peek = deserializer->reader->peek();
                }
                case State::PEEK: {
                    AWAIT_PTR(peek, result)
                    if (result.isEmpty()) {
                        READY(false)
                    }
                    char c = result.get();

                    if (c == '}') {
                        goto initReadClosingBracket;
                    }
                    if (firstValue) {
                        firstValue = false;
                        goto initNameWhitespaces;
                    }

                    INIT_AWAIT(READ_COMMA, readChar,
                               ReadChar(deserializer->reader), result)
                    // result will exist
                }
                initNameWhitespaces : {
                    state = State::READ_NAME_WHITESPACES;
                    this->readWhile = ReadWhile<bool (*)(char)>(
                        deserializer->reader, isCharWhitespace);
                }
                case State::READ_NAME_WHITESPACES: {
                    AWAIT(readWhile, result)
                    // Result is void_

                    INIT_AWAIT(READ_NAME, readName,
                               ReadJsonString<ReadIntoNameStore>(
                                   deserializer->reader,
                                   ReadIntoNameStore{.nameStore = &nameStore}),
                               result)
                    if (!result) {
                        READY(false)
                    }

                    INIT_AWAIT(READ_COLON_WHITESPACES, readWhile,
                               ReadWhile<bool (*)(char)>(deserializer->reader,
                                                         isCharWhitespace),
                               result)
                    // Result is void_

                    INIT_AWAIT(READ_COLON, readChar,
                               ReadChar(deserializer->reader), result)
                    if (result.isEmpty()) {
                        READY(false)
                    }
                    char c = result.get();
                    if (c != ':') {
                        READY(false)
                    }

                    auto future = ReadWhile<bool (*)(char)>(
                        deserializer->reader, isCharWhitespace);
                    INIT_AWAIT(READ_VISITOR_WHITESPACES, readWhile, future,
                               outputName)

                    typename StructVisitor::VisitFuture future =
                        visitor.visit(&nameStore, deserializer);

                    nameStore.clear();

                    INIT_AWAIT(READ_VISITOR, visitFuture, future, result)
                    if (!result) {
                        READY(false)
                    }

                    goto initPeekWhitespaces;
                }
                initReadClosingBracket : {
                    state = State::READ_CLOSING_BRACKET;
                    readChar = ReadChar(deserializer->reader);
                }
                case State::READ_CLOSING_BRACKET: {
                    AWAIT(readChar, result)
                    if (result.isEmpty()) {
                        READY(false)
                    }
                    // We know it's a closing bracket

                    READY(true)
                }
            }
            return Poll<bool>::pending();
        }

       private:
        enum class State {
            INIT,
            READ_WHITESPACES,
            READ_OPEN_BRACKET,
            READ_BEFORE_PEEK_WHITESPACES,
            PEEK,
            READ_COMMA,
            READ_NAME_WHITESPACES,
            READ_NAME,
            READ_COLON_WHITESPACES,
            READ_COLON,
            READ_VISITOR_WHITESPACES,
            READ_VISITOR,
            READ_CLOSING_BRACKET,
        } state = State::INIT;
        bool firstValue = true;
        JsonDeserializer *deserializer;
        NameStore nameStore;
        StructVisitor visitor;
        union {
            Peek *peek;
            ReadChar readChar;
            ReadWhile<bool (*)(char)> readWhile;
            ReadJsonString<ReadIntoNameStore> readName;
            typename StructVisitor::VisitFuture visitFuture;
        };
    };

    template <typename NameStore, typename StructVisitor>
    DeserializeStructFuture<NameStore, StructVisitor> deserializeStruct(
        NameStore nameStore, StructVisitor visitor) {
        return DeserializeStructFuture<NameStore, StructVisitor>(
            this, nameStore, visitor);
    }

   private:
    Reader *reader;
};

template <typename T>
class DeserializeJson : Future<DeserializeJson<T>, Optional<T>> {
   public:
    explicit DeserializeJson(Reader *reader)
        : deserializer(JsonDeserializer(reader)) {}

    Poll<Optional<T>> poll() {
        switch (state) {
            case State::INIT: {
                auto futureValue =
                    deser::Deserialize<T, JsonDeserializer>::deserialize(
                        &deserializer);
                INIT_AWAIT(POLL, future, futureValue, result)
                READY(result)
            }
        }
        return Poll<Optional<T>>::pending();
    }

   private:
    enum class State { INIT, POLL } state = State::INIT;
    JsonDeserializer deserializer;
    union {
        void_ none;
        typename deser::Deserialize<T, JsonDeserializer>::DeserializeFuture
            future;
    };
};

#define IMPL_JSON_DESERIALIZE_NUMBER(number_)                                  \
    template <>                                                                \
    struct deser::Deserialize<number_, JsonDeserializer> {                     \
        class DeserializeFuture                                                \
            : Future<DeserializeFuture, Optional<number_>> {                   \
           public:                                                             \
            explicit DeserializeFuture(Reader *reader)                         \
                : future(ReadNumber(reader)) {}                                \
                                                                               \
            Poll<Optional<number_>> poll() {                                   \
                auto poll = future.poll();                                     \
                if (poll.isReady()) {                                          \
                    auto opt = poll.get();                                     \
                    if (opt.isEmpty()) {                                       \
                        READY(Optional<number_>::empty())                      \
                    } else {                                                   \
                        READY(Optional<number_>::of(opt.get()))                \
                    }                                                          \
                } else {                                                       \
                    return Poll<Optional<number_>>::pending();                 \
                }                                                              \
            }                                                                  \
                                                                               \
           private:                                                            \
            ReadNumber future;                                                 \
        };                                                                     \
                                                                               \
        static DeserializeFuture deserialize(JsonDeserializer *deserializer) { \
            return DeserializeFuture(deserializer->getReader());               \
        }                                                                      \
    };

IMPL_JSON_DESERIALIZE_NUMBER(short)
IMPL_JSON_DESERIALIZE_NUMBER(int)
IMPL_JSON_DESERIALIZE_NUMBER(long)
IMPL_JSON_DESERIALIZE_NUMBER(float)
IMPL_JSON_DESERIALIZE_NUMBER(double)

template <>
struct deser::Deserialize<bool, JsonDeserializer> {
    class DeserializeFuture : Future<DeserializeFuture, Optional<bool>> {
       public:
        DeserializeFuture(JsonDeserializer *deserializer)
            : init({deserializer}) {}

        Poll<Optional<bool>> poll() {
            switch (state) {
                case State::INIT: {
                    INIT_AWAIT(
                        POLL, readBool,
                        ReadJsonBoolean(init.deserializer->getReader(), buffer),
                        result)
                    READY(result)
                }
            }
            return Poll<Optional<bool>>::pending();
        }

       private:
        enum class State { INIT, POLL } state = State::INIT;
        char buffer[5];
        union {
            struct {
                JsonDeserializer *deserializer;
            } init;
            ReadJsonBoolean readBool;
        };
    };

    static DeserializeFuture deserialize(JsonDeserializer *deserializer) {
        return DeserializeFuture(deserializer);
    }
};

template <size_t Capacity>
struct deser::Deserialize<SizedBuffer<Capacity>, JsonDeserializer> {
    class DeserializeFuture
        : Future<DeserializeFuture, Optional<SizedBuffer<Capacity>>> {
       public:
        DeserializeFuture(JsonDeserializer *deserializer)
            : init({deserializer}) {}

        Poll<Optional<SizedBuffer<Capacity>>> poll() {
            switch (state) {
                case State::INIT: {
                    Reader *reader = init.deserializer->getReader();

                    auto future = ReadJsonString<ReadIntoBuffer>(
                        reader, ReadIntoBuffer{});
                    INIT_AWAIT(POLL, readString, future, result)

                    if (!result) {
                        READY(Optional<SizedBuffer<Capacity>>::empty())
                    }
                    READY(Optional<SizedBuffer<Capacity>>::of(
                        readString.getFunc()->buffer))
                }
            }
            return Poll<Optional<SizedBuffer<Capacity>>>::pending();
        }

       private:
        enum class State { INIT, POLL } state = State::INIT;
        struct ReadIntoBuffer {
            SizedBuffer<Capacity> buffer = SizedBuffer<Capacity>();
            bool operator()(char c) { return buffer.push(c); }
        };
        union {
            struct {
                JsonDeserializer *deserializer;
            } init;
            ReadJsonString<ReadIntoBuffer> readString;
        };
    };

    static DeserializeFuture deserialize(JsonDeserializer *deserializer) {
        return DeserializeFuture(deserializer);
    }
};

#include "ser.h"
#include "writer.h"

class WriteJsonBoolean : WriteFuture<WriteJsonBoolean, bool> {
   public:
    WriteJsonBoolean(Writer *writer, bool value)
        : init({writer}), value(value) {}

    Poll<bool> poll() {
        switch (state) {
            case State::INIT: {
                Writer *writer = init.writer;

                state = State::WRITE;
                BufferRef valueStr = getValueStr();
                write = writer->writeFromBuffer(valueStr.data, valueStr.length);
            }
            case State::WRITE: {
                AWAIT_PTR(write, result)
                if (result != getValueStr().length) {
                    READY(false)
                }

                READY(true)
            }
        }
        return Poll<bool>::pending();
    }

   private:
    BufferRef getValueStr() {
        return value ? BufferRef(JsonConsts::TRUE)
                     : BufferRef(JsonConsts::FALSE);
    }

    enum class State {
        INIT,
        WRITE,
    } state = State::INIT;
    bool value;
    union {
        struct {
            Writer *writer;
        } init;
        WriteFromBuffer *write;
    };
};

class WriteJsonString : WriteFuture<WriteJsonString, bool> {
    // TODO: String escaping
   public:
    WriteJsonString(Writer *writer, BufferRef string)
        : init({writer}), string(string) {}

    Writer *getWriter() {
        switch (state) {
            case State::INIT:
                return init.writer;
            case State::WRITE_BEGIN:
                return writeChar.getWriter();
            case State::WRITE_STRING:
                return write->getWriter();
            case State::WRITE_END:
                return writeChar.getWriter();
        }
        return nullptr;
    }

    Poll<bool> poll() {
        switch (state) {
            case State::INIT: {
                Writer *writer = init.writer;

                state = State::WRITE_BEGIN;
                writeChar = WriteChar(writer, '"');
            }
            case State::WRITE_BEGIN: {
                AWAIT(writeChar, result)
                if (!result) {
                    return Poll<bool>::ready(false);
                }

                Writer *writer = writeChar.getWriter();

                state = State::WRITE_STRING;
                write = writer->writeFromBuffer(string.data, string.length);
            }
            case State::WRITE_STRING: {
                AWAIT_PTR(write, result)
                if (result != string.length) {
                    return Poll<bool>::ready(false);
                }

                Writer *writer = write->getWriter();

                state = State::WRITE_END;
                writeChar = WriteChar(writer, '"');
            }
            case State::WRITE_END: {
                AWAIT(writeChar, result)
                if (!result) {
                    return Poll<bool>::ready(false);
                }

                return Poll<bool>::ready(true);
            }
        }
        return Poll<bool>::pending();
    }

   private:
    enum class State {
        INIT,
        WRITE_BEGIN,
        WRITE_STRING,
        WRITE_END
    } state = State::INIT;

    BufferRef string;
    union {
        struct {
            Writer *writer;
        } init;
        WriteChar writeChar;
        WriteFromBuffer *write;
    };
};

class JsonSerializer {
   public:
    JsonSerializer(Writer *writer) : writer(writer) {}

    Writer *getWriter() { return writer; }

    class SerializeStruct {
       public:
        SerializeStruct(JsonSerializer *serializer) : serializer(serializer) {}

        template <typename T>
        class SerializeFieldFuture : Future<SerializeFieldFuture<T>, bool> {
           public:
            SerializeFieldFuture(SerializeStruct *serializeStruct,
                                 BufferRef name, T *value)
                : serializeStruct(serializeStruct), name(name), value(value) {}

            Poll<bool> poll() {
                switch (state) {
                    case State::INIT: {
                        if (serializeStruct->isFirstElement) {
                            serializeStruct->isFirstElement = false;
                            goto writeKeyInit;
                        }

                        INIT_AWAIT(
                            WRITE_COMMA, writeChar,
                            WriteChar(serializeStruct->serializer->writer, ','),
                            result)
                        if (!result) {
                            READY(false)
                        }
                    }
                    writeKeyInit : {
                        this->state = State::WRITE_KEY;
                        this->writeJsonString = WriteJsonString(
                            serializeStruct->serializer->writer, name);
                    }
                    case State::WRITE_KEY: {
                        AWAIT(writeJsonString, result)
                        if (!result) {
                            READY(false)
                        }

                        INIT_AWAIT(
                            WRITE_COLON, writeChar,
                            WriteChar(serializeStruct->serializer->writer, ':'),
                            result)
                        if (!result) {
                            READY(false)
                        }

                        auto serialize_ =
                            Serialize<T, JsonSerializer>::serialize(
                                serializeStruct->serializer, value);
                        INIT_AWAIT(WRITE_VALUE, writeValue, serialize_, result)
                        if (!result) {
                            READY(false)
                        }

                        READY(true)
                    }
                }
                return Poll<bool>::pending();
            }

           private:
            enum class State {
                INIT,
                WRITE_COMMA,
                WRITE_KEY,
                WRITE_COLON,
                WRITE_VALUE
            } state = State::INIT;
            SerializeStruct *serializeStruct;

            BufferRef name;
            T *value;
            union {
                WriteJsonString writeJsonString;
                WriteChar writeChar;
                typename Serialize<T, JsonSerializer>::SerializeFuture
                    writeValue;
            };
        };
        template <typename T>
        SerializeFieldFuture<T> serializeField(BufferRef name, T *value) {
            return SerializeFieldFuture<T>(this, name, value);
        }

        typedef WriteChar EndFuture;
        EndFuture end() { return WriteChar(this->serializer->writer, '}'); }

       private:
        bool isFirstElement = true;
        JsonSerializer *serializer;
    };

    class SerializeStructFuture
        : Future<SerializeStructFuture, SerializeStruct> {
       public:
        SerializeStructFuture(JsonSerializer *serializer)
            : serializer(serializer), writeChar(serializer->writer, '{') {}

        Poll<Optional<SerializeStruct>> poll() {
            Poll<bool> poll = writeChar.poll();
            if (poll.isPending()) {
                return Poll<Optional<SerializeStruct>>::pending();
            }
            bool result = poll.get();
            if (!result) {
                return Poll<Optional<SerializeStruct>>::ready(
                    Optional<SerializeStruct>::empty());
            }

            return Poll<Optional<SerializeStruct>>::ready(
                Optional<SerializeStruct>::of(
                    SerializeStruct(this->serializer)));
        }

       private:
        JsonSerializer *serializer;
        WriteChar writeChar;
    };

    SerializeStructFuture serializeStruct() {
        return SerializeStructFuture(this);
    }

   private:
    Writer *writer;
};

template <typename T>
class SerializeJson : Future<SerializeJson<T>, bool> {
   public:
    SerializeJson(Writer *writer, T *ptr)
        : serializer(JsonSerializer(writer)),
          future(Serialize<T, JsonSerializer>::serialize(&this->serializer,
                                                         ptr)) {}

    Poll<bool> poll() { return future.poll(); }

   private:
    JsonSerializer serializer;
    typename Serialize<T, JsonSerializer>::SerializeFuture future;
};

#define IMPL_JSON_SERIALIZE_NUMBER(number_)                           \
    template <>                                                       \
    struct Serialize<number_, JsonSerializer> {                       \
        typedef WriteDouble<10> SerializeFuture;                      \
                                                                      \
        static SerializeFuture serialize(JsonSerializer *serializer,  \
                                         number_ *number) {           \
            return WriteDouble<10>(serializer->getWriter(), *number); \
        }                                                             \
    };

IMPL_JSON_SERIALIZE_NUMBER(short)
IMPL_JSON_SERIALIZE_NUMBER(int)
IMPL_JSON_SERIALIZE_NUMBER(long)
IMPL_JSON_SERIALIZE_NUMBER(float)
IMPL_JSON_SERIALIZE_NUMBER(double)

template <>
struct Serialize<bool, JsonSerializer> {
    typedef WriteJsonBoolean SerializeFuture;

    static WriteJsonBoolean serialize(JsonSerializer *serializer, bool *value) {
        return WriteJsonBoolean(serializer->getWriter(), *value);
    }
};

template <>
struct Serialize<BufferRef, JsonSerializer> {
    typedef WriteJsonString SerializeFuture;

    static SerializeFuture serialize(JsonSerializer *serializer,
                                     BufferRef *value) {
        return WriteJsonString(serializer->getWriter(), *value);
    }
};

#endif
