#ifndef CPP_ASYNC_HTTP_HTTP_H
#define CPP_ASYNC_HTTP_HTTP_H

#include <new>

#include "future.h"
#include "reader.h"
#include "utils.h"
#include "writer.h"

// Official RFC: https://tools.ietf.org/html/rfc2616
// We only support till http2.0 because its much easier.

namespace HttpConsts {
const char *GET = "GET";
const char *POST = "POST";
const char *PUT = "PUT";
const char *DELETE = "DELETE";
const char *HEAD = "HEAD";
const char *OPTIONS = "OPTIONS";
const char *TRACE = "TRACE";
const char *CONNECT = "CONNECT";
const char *PATCH = "PATCH";

const char *HTTP_1_0 = "HTTP/1.0";
const char *HTTP_1_1 = "HTTP/1.1";
const char *HTTP_2_0 = "HTTP/2.0";
}  // namespace HttpConsts

enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE,
    HEAD,
    OPTIONS,
    TRACE,
    CONNECT,
    PATCH,
};

enum class HttpVersion { HTTP_1_0, HTTP_1_1, HTTP_2_0 };

template <size_t PathLength>
struct BufferHttpPathStore {
    SizedBuffer<PathLength> path = SizedBuffer<PathLength>();

    bool push(char c) { return path.push(c); }
};

struct HttpRequestStatusLine {
    HttpVersion version;
    HttpMethod method;
};

template <typename PathStore>
class ReadHttpRequestStatusLine : Future<ReadHttpRequestStatusLine<PathStore>,
                                         Optional<HttpRequestStatusLine>> {
   public:
    ReadHttpRequestStatusLine(Reader *reader, PathStore *pathStore)
        : init{reader}, pathStore(pathStore) {}

    Poll<Optional<HttpRequestStatusLine>> poll() {
        switch (state) {
            case State::INIT: {
                INIT_AWAIT(READ_METHOD, readIntoBuffer,
                           ReadIntoWhile<bool (*)(char)>(init.reader, buffer,
                                                         BUFFER_LENGTH,
                                                         isCharNotWhitespace),
                           len)
                BufferRef buf = BufferRef(buffer, len);
                if (buf == HttpConsts::GET) {
                    statusLine.method = HttpMethod::GET;
                } else if (buf == HttpConsts::POST) {
                    statusLine.method = HttpMethod::POST;
                } else if (buf == HttpConsts::PUT) {
                    statusLine.method = HttpMethod::PUT;
                } else if (buf == HttpConsts::DELETE) {
                    statusLine.method = HttpMethod::DELETE;
                } else if (buf == HttpConsts::HEAD) {
                    statusLine.method = HttpMethod::HEAD;
                } else if (buf == HttpConsts::OPTIONS) {
                    statusLine.method = HttpMethod::OPTIONS;
                } else if (buf == HttpConsts::TRACE) {
                    statusLine.method = HttpMethod::TRACE;
                } else if (buf == HttpConsts::CONNECT) {
                    statusLine.method = HttpMethod::CONNECT;
                } else if (buf == HttpConsts::PATCH) {
                    statusLine.method = HttpMethod::PATCH;
                } else {
                    READY(Optional<HttpRequestStatusLine>::empty())
                }

                Reader *reader = readIntoBuffer.getReader();

                INIT_AWAIT(READ_REQUEST_SPACE, readChar, ReadChar(reader),
                           result)
                if (result.isEmpty()) {
                    READY(Optional<HttpRequestStatusLine>::empty())
                }
                if (result.get() != ' ') {
                    READY(Optional<HttpRequestStatusLine>::empty())
                }

                Reader *reader = readChar.getReader();

                auto future = ReadIntoStoreWhile<PathStore, bool (*)(char)>(
                    reader, pathStore, isCharNotWhitespace);
                INIT_AWAIT(READ_REQUEST, readIntoPathStoreWhile, future, result)
                // result is void_

                Reader *reader = readIntoPathStoreWhile.getReader();

                INIT_AWAIT(READ_VERSION_SPACE, readChar, ReadChar(reader),
                           result)
                if (result.isEmpty()) {
                    READY(Optional<HttpRequestStatusLine>::empty())
                }
                if (result.get() != ' ') {
                    READY(Optional<HttpRequestStatusLine>::empty())
                }

                Reader *reader = readChar.getReader();

                INIT_AWAIT(
                    READ_VERSION, readIntoBuffer,
                    ReadIntoWhile<bool (*)(char)>(reader, buffer, BUFFER_LENGTH,
                                                  isCharNotWhitespace),
                    len)
                BufferRef buf = BufferRef(buffer, len);

                if (buf == HttpConsts::HTTP_1_0) {
                    statusLine.version = HttpVersion::HTTP_1_0;
                } else if (buf == HttpConsts::HTTP_1_1) {
                    statusLine.version = HttpVersion::HTTP_1_1;
                } else if (buf == HttpConsts::HTTP_2_0) {
                    statusLine.version = HttpVersion::HTTP_2_0;
                } else {
                    READY(Optional<HttpRequestStatusLine>::empty())
                }

                Reader *reader = readIntoBuffer.getReader();

                INIT_AWAIT(READ_CRLF, readCrlf, ReadCrlf(reader), result)
                if (result.isEmpty()) {
                    READY(Optional<HttpRequestStatusLine>::empty())
                }
                if (!result.get()) {
                    READY(Optional<HttpRequestStatusLine>::empty())
                }

                READY(Optional<HttpRequestStatusLine>::of(statusLine))
            }
        }
        return Poll<Optional<HttpRequestStatusLine>>::pending();
    }

   private:
    static const size_t MAX_METHOD_LENGTH = 7;
    static const size_t MAX_VERSION_LENGTH = 8;
    static const size_t BUFFER_LENGTH =
        max(MAX_METHOD_LENGTH, MAX_VERSION_LENGTH);

    enum class State {
        INIT,
        READ_METHOD,
        READ_REQUEST_SPACE,
        READ_REQUEST,
        READ_VERSION_SPACE,
        READ_VERSION,
        READ_CRLF
    } state = State::INIT;
    PathStore *pathStore;
    // TODO: optimize for memory
    char buffer[BUFFER_LENGTH];

    HttpRequestStatusLine statusLine;
    union {
        struct {
            Reader *reader;
        } init;
        ReadChar readChar;
        ReadIntoWhile<bool (*)(char)> readIntoBuffer;
        ReadIntoStoreWhile<PathStore, bool (*)(char)> readIntoPathStoreWhile;
        ReadCrlf readCrlf;
    };
};

template <typename HeaderNameStore, typename HeaderValueStore,
          typename HeaderVisitor>
class ReadHttpHeaders
    : ReadFuture<
          ReadHttpHeaders<HeaderNameStore, HeaderValueStore, HeaderVisitor>,
          bool> {
   public:
    ReadHttpHeaders(Reader *reader, HeaderNameStore nameStore,
                    HeaderValueStore valueStore, HeaderVisitor visitor)
        : init{reader},
          nameStore(nameStore),
          valueStore(valueStore),
          visitor(visitor) {}

    Reader *getReader() {
        switch (state) {
            case State::INIT:
                return init.reader;
            case State::HEADER_NAME_STORE:
                return readNameWhile.getReader();
            case State::HEADER_TO_COLON:
                return readWhile.getReader();
            case State::HEADER_COLON:
                return readChar.getReader();
            case State::HEADER_VALUE_SPACES:
                return readWhile.getReader();
            case State::HEADER_VALUE_STORE:
                return readValueWhile.getReader();
            case State::HEADER_VISITOR:
                return visit.reader;
            case State::HEADER_TO_CR:
                return readWhile.getReader();
            case State::HEADER_CRLF:
                return readCrlf.getReader();
            case State::HEADERS_END_CRLF:
                return readCrlf.getReader();
        }
        return nullptr;
    }

    Poll<bool> poll() {
        switch (state) {
            case State::INIT: {
            init : {}
                headerSuccessfullyParsed = true;

                Reader *reader = getReader();

                auto future =
                    ReadIntoStoreWhile<HeaderNameStore, bool (*)(char)>(
                        reader, &nameStore, isCharNotWhitespaceOrColon);
                INIT_AWAIT(HEADER_NAME_STORE, readNameWhile, future, result)
                if (!result) {
                    headerSuccessfullyParsed = false;
                }

                Reader *reader = readNameWhile.getReader();
                INIT_AWAIT(HEADER_TO_COLON, readWhile,
                           ReadWhile<bool (*)(char)>(reader, isCharNotColon),
                           result)
                // result is void_

                Reader *reader = readWhile.getReader();
                INIT_AWAIT(HEADER_COLON, readChar, ReadChar(reader), result)
                if (result.isEmpty()) {
                    READY(false)
                }
                char c = result.get();
                if (c != ':') {
                    READY(false)
                }

                Reader *reader = readChar.getReader();
                INIT_AWAIT(HEADER_VALUE_SPACES, readWhile,
                           ReadWhile<bool (*)(char)>(reader, isCharWhitespace),
                           result)

                Reader *reader = readWhile.getReader();
                auto future =
                    ReadIntoStoreWhile<HeaderValueStore, bool (*)(char)>(
                        reader, &valueStore, isCharNotWhitespace);
                INIT_AWAIT(HEADER_VALUE_STORE, readValueWhile, future, result)
                if (!result) {
                    headerSuccessfullyParsed = false;
                }

                if (!headerSuccessfullyParsed) {
                    goto initHeaderToCr;
                }

                Reader *reader = readValueWhile.getReader();
                state = State::HEADER_VISITOR;
                visit = {reader, visitor.visit(&nameStore, &valueStore)};
            }
            case State::HEADER_VISITOR: {
                AWAIT(visit.future, result)
            }
            initHeaderToCr : {
                nameStore.clear();
                valueStore.clear();

                Reader *reader = getReader();
                state = State::HEADER_TO_CR;
                readWhile = ReadWhile<bool (*)(char)>(reader, isCharNotCr);
            }
            case State::HEADER_TO_CR: {
                AWAIT(readWhile, result)
                // result is void_

                Reader *reader = readWhile.getReader();
                INIT_AWAIT(HEADER_CRLF, readCrlf, ReadCrlf(reader), result)
                if (result.isEmpty() || !result.get()) {
                    READY(false)
                }

                Reader *reader = readCrlf.getReader();
                INIT_AWAIT(HEADERS_END_CRLF, readCrlf, ReadCrlf(reader), result)
                if (result.isEmpty()) {
                    READY(false)
                }
                bool isHeaderEnd = result.get();
                if (!isHeaderEnd) {
                    goto init;
                }

                READY(true)
            }
        }
        return Poll<bool>::pending();
    }

   private:
    static bool isCharNotColon(char c) { return c != ':'; }
    static bool isCharNotWhitespaceOrColon(char c) {
        return isCharNotWhitespace(c) && isCharNotColon(c);
    }
    static bool isCharNotCr(char c) { return c != '\r'; }

    enum class State {
        INIT,
        // TODO: is read whitespaces before header name required?
        HEADER_NAME_STORE,
        HEADER_TO_COLON,
        HEADER_COLON,
        HEADER_VALUE_SPACES,
        HEADER_VALUE_STORE,
        HEADER_VISITOR,
        HEADER_TO_CR,
        HEADER_CRLF,
        HEADERS_END_CRLF
    } state = State::INIT;
    HeaderNameStore nameStore;
    HeaderValueStore valueStore;
    HeaderVisitor visitor;
    bool headerSuccessfullyParsed;  // This says if there was enough space to
                                    // fill the header/value store. If this is
                                    // false it means there wasn't and therefore
                                    // HeaderVisitors visitFuture shouldn't be
                                    // called.
    union {
        struct {
            Reader *reader;
        } init;
        ReadIntoStoreWhile<HeaderNameStore, bool (*)(char)> readNameWhile;
        ReadIntoStoreWhile<HeaderValueStore, bool (*)(char)> readValueWhile;
        ReadWhile<bool (*)(char)> readWhile;
        ReadChar readChar;
        ReadCrlf readCrlf;

        struct {
            Reader *reader;
            typename HeaderVisitor::VisitFuture future;
        } visit;
    };
};

struct HttpResponseStatusLine {
    HttpVersion httpVersion;
    unsigned short code;
};

class WriteHttpResponseStatusLine : Future<WriteHttpResponseStatusLine, bool> {
   public:
    WriteHttpResponseStatusLine(Writer *writer,
                                HttpResponseStatusLine statusLine,
                                BufferRef reason)
        : init({writer}), statusLine(statusLine), reason(reason) {}

    Poll<bool> poll() {
        switch (state) {
            case State::INIT: {
                BufferRef buffer = getVersionBuffer();

                Writer *writer = init.writer;
                state = State::WRITE_VERSION;
                writeFromBuffer =
                    writer->writeFromBuffer(buffer.data, buffer.length);
            }
            case State::WRITE_VERSION: {
                AWAIT_PTR(writeFromBuffer, result)
                if (result != getVersionBuffer().length) {
                    READY(false)
                }

                Writer *writer = writeFromBuffer->getWriter();
                INIT_AWAIT(WRITE_CODE_SPACE, writeChar, WriteChar(writer, ' '),
                           result)

                if (!result) {
                    READY(false)
                }

                Writer *writer = writeChar.getWriter();
                INIT_AWAIT(WRITE_CODE, writeDouble,
                           WriteDouble<10>(writer, statusLine.code), result)
                if (!result) {
                    READY(false)
                }

                Writer *writer = writeDouble.getWriter();
                INIT_AWAIT(WRITE_REASON_SPACE, writeChar,
                           WriteChar(writer, ' '), result)
                if (!result) {
                    READY(false)
                }

                Writer *writer = writeChar.getWriter();
                state = State::WRITE_REASON;
                writeFromBuffer =
                    writer->writeFromBuffer(reason.data, reason.length);
            }
            case State::WRITE_REASON: {
                AWAIT_PTR(writeFromBuffer, result)
                if (result != reason.length) {
                    READY(false)
                }

                Writer *writer = writeFromBuffer->getWriter();
                INIT_AWAIT(WRITE_CRLF, writeCrlf, WriteCrlf(writer), result)
                if (!result) {
                    READY(false)
                }

                READY(true)
            }
        }
        return Poll<bool>::pending();
    }

   private:
    BufferRef getVersionBuffer() {
        BufferRef buffer;
        switch (statusLine.httpVersion) {
            case HttpVersion::HTTP_1_0:
                buffer = BufferRef(HttpConsts::HTTP_1_0);
                break;
            case HttpVersion::HTTP_1_1:
                buffer = BufferRef(HttpConsts::HTTP_1_1);
                break;
            case HttpVersion::HTTP_2_0:
                buffer = BufferRef(HttpConsts::HTTP_2_0);
                break;
            default:
                buffer = BufferRef(HttpConsts::HTTP_1_0);
                break;
        }
        return buffer;
    }

    enum class State {
        INIT,
        WRITE_VERSION,
        WRITE_CODE_SPACE,
        WRITE_CODE,
        WRITE_REASON_SPACE,
        WRITE_REASON,
        WRITE_CRLF
    } state = State::INIT;
    HttpResponseStatusLine statusLine;
    BufferRef reason;
    union {
        struct {
            Writer *writer;
        } init;
        WriteDouble<10> writeDouble;
        WriteChar writeChar;
        WriteFromBuffer *writeFromBuffer;
        WriteCrlf writeCrlf;
    };
};

#endif