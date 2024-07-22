#ifndef CPP_ASYNC_HTTP_HTTP_HANDLER_H
#define CPP_ASYNC_HTTP_HTTP_HANDLER_H

#include "buffer.h"
#include "http.h"
#include "json.h"
#include "reader.h"
#include "utils.h"
#include "writer.h"

class HttpRequest {
   public:
    HttpRequest(Reader* reader, Writer* writer)
        : bodyReader(reader), responseWriter(writer) {}

    Optional<Reader*> tryTakeBody() {
        if (!bodyTaken) {
            bodyTaken = true;
            return Optional<Reader*>::of(bodyReader);
        }
        return Optional<Reader*>::empty();
    }

    bool isBodyTaken() { return bodyTaken; }

    Writer* writeResponse() {
        responseWritten = true;
        return responseWriter;
    }

    bool isResponseWritten() { return responseWritten; }

   private:
    bool bodyTaken = false;
    Reader* bodyReader;
    bool responseWritten = false;
    Writer* responseWriter;
};

// This struct can extract data from the http request
template <typename T>
struct http_extractor {
    static T createExtractor() = delete;

    static bool extractStatusLine(T* extractor,
                                  HttpRequestStatusLine statusLine) {
        return true;
    }

    static const size_t MAX_HEADER_NAME = 0;
    static const size_t MAX_HEADER_VALUE = 0;

    static void extractHeader(T* extractor, BufferRef headerName,
                              BufferRef headerValue) {
        return;
    }

    typedef Future<void_, void_> ExtractRequestFuture;

    static ExtractRequestFuture extractRequest(T* extractor,
                                               HttpRequest* request) = delete;
};

template <typename T>
struct http_response {
    typedef Future<void_, void_> RespondFuture;

    RespondFuture respond(Writer* writer, T response) = delete;
};

template <typename T>
struct http_handler {
    typedef template_utils::pack<> Extractors;
    // Must implement http_response
    typedef void_ Response;

    typedef Future<void_, Response> HandleFuture;
    static HandleFuture handle(Extractors extractors) = delete;
};

// Simple http request/response handler template

class HeaderVisitorExample {
    // template parameters to ReadHttpHeaders
    typedef void_ HeaderNameStore;
    typedef void_ HeaderValueStore;

    // required definitions:
    typedef Future<void_, void_> VisitFuture;

    VisitFuture visit(HeaderNameStore* nameStore, HeaderValueStore* valueStore);
};

template <typename Handler,
          typename = typename http_handler<Handler>::Extractors>
class HandleHttpRequest : Future<void_, void_> {};

template <typename Handler, typename... Extractors>
class HandleHttpRequest<Handler, template_utils::pack<Extractors...>> {
   private:
    typedef typename http_handler<Handler>::HandleFuture HandleFuture;
    typedef typename http_handler<Handler>::Response Response;

    static constexpr const size_t extractorsLength =
        template_utils::pack<Extractors...>::length;

    static constexpr const size_t MAX_HEADER_NAME = template_utils::max_value<
        size_t, 0, http_extractor<Extractors>::MAX_HEADER_NAME...>::value;
    static constexpr const size_t MAX_HEADER_VALUE = template_utils::max_value<
        size_t, 0, http_extractor<Extractors>::MAX_HEADER_VALUE...>::value;

   public:
    HandleHttpRequest(HttpRequestStatusLine statusLine, Reader* reader,
                      Writer* writer)
        : reader(reader), writer(writer), init({statusLine}) {}

    Poll<void_> poll() {
        switch (state) {
            case State::INIT: {
                extractors = Tuple<Extractors...>(
                    http_extractor<Extractors>::createExtractor()...);
                if constexpr (Tuple<Extractors...>::length > 0) {
                    extractStatusLine(init.statusLine, &extractors);
                }

                auto future = ReadHttpHeaders<SizedBuffer<MAX_HEADER_NAME>,
                                              SizedBuffer<MAX_HEADER_VALUE>,
                                              HeaderVisitor>(
                    reader, SizedBuffer<MAX_HEADER_NAME>(),
                    SizedBuffer<MAX_HEADER_VALUE>(),
                    HeaderVisitor{.handle = this});
                INIT_AWAIT(HEADERS, readHeaders, future, result)
                if (!result) {
                    READY(false)
                }

                state = State::EXTRACT;
                extractFutures.extractor = 0;
                extractFutures.request = HttpRequest(reader, writer);
            }
            case State::EXTRACT: {
                Poll<bool> poll = extractPoll();
                if (poll.isPending()) {
                    return Poll<void_>::pending();
                }
                bool result = poll.get();
                if (!result) {
                    READY(void_())
                }
                if (extractFutures.request.isResponseWritten()) {
                    READY(void_())
                }

                struct HandleFutureCaller {
                    HandleFuture call(Extractors... extractors) {
                        return http_handler<Handler>::handle(extractors...);
                    }
                };
                HandleFuture future =
                    extractors.template call<HandleFutureCaller, HandleFuture>(
                        HandleFutureCaller());
                INIT_AWAIT(HANDLE, handleFuture, future, response)

                auto future =
                    http_response<Response>::respond(writer, response);
                INIT_AWAIT(RESPOND, respondFuture, future, result)

                READY(void_())
            }
        }
        return Poll<void_>::pending();
    }

   private:
    template <typename T, typename... Ts>
    static inline void extractStatusLine(HttpRequestStatusLine statusLine,
                                         Tuple<T, Ts...>* tuple) {
        T* extractor = tuple->template atPtr<0>();
        http_extractor<T>::extractStatusLine(extractor, statusLine);

        if constexpr (template_utils::pack<Ts...>::length > 1) {
            extractStatusLine(statusLine, tuple->asNext());
        }
    }

    template <size_t ExtractorIndex, typename T, typename... Ts>
    inline Poll<bool> extractPoll2() {
        // check if the next extractor should be called
        if constexpr (ExtractorIndex < extractorsLength - 1) {
            if (this->extractFutures.extractor > ExtractorIndex) {
                return extractPoll2<ExtractorIndex + 1, Ts...>();
            }
        }
        typedef typename http_extractor<T>::ExtractRequestFuture ExtractFuture;

        if (this->extractFutures.extractor == ExtractorIndex - 1) {
            // we're initializing
            T* pExtractor = extractors.template atPtr<ExtractorIndex - 1>();
            ExtractFuture future = http_extractor<T>::extractRequest(
                pExtractor, &this->extractFutures.request);
            this->extractFutures.future.template setPtr<ExtractFuture>(&future);

            this->extractFutures.extractor = ExtractorIndex;
        }

        Poll<void_> poll =
            this->extractFutures.future.template asPtr<ExtractFuture>()->poll();
        if (poll.isPending()) {
            return Poll<bool>::pending();
        }
        if (this->extractFutures.request.isResponseWritten()) {
            return Poll<bool>::ready(false);
        }

        if constexpr (ExtractorIndex >= extractorsLength) {
            return Poll<bool>::ready(true);
        } else {
            return extractPoll2<ExtractorIndex + 1, Ts...>();
        }
    }
    inline Poll<bool> extractPoll() {
        if constexpr (template_utils::pack<Extractors...>::length >= 1) {
            return extractPoll2<1, Extractors...>();
        } else {
            return Poll<bool>::ready(true);
        }
    }

    struct HeaderVisitor {
        typedef Instant<void_> VisitFuture;
        HandleHttpRequest* handle;

        template <typename T, typename... Ts>
        inline static void extractHeader(BufferRef name, BufferRef value,
                                         Tuple<T, Ts...>* tuple) {
            auto extractor = tuple->template atPtr<0>();

            http_extractor<T>::extractHeader(extractor, name, value);

            if constexpr (tuple->length > 1) {
                extractHeader<Ts...>(name, value, tuple->asNext());
            }
        }
        VisitFuture visit(SizedBuffer<MAX_HEADER_NAME>* name,
                          SizedBuffer<MAX_HEADER_VALUE>* value) {
            if constexpr (template_utils::pack<Extractors...>::length > 0) {
                extractHeader<Extractors...>(name->asRef(), value->asRef(),
                                             &handle->extractors);
            }
            return Instant<void_>(void_());
        }
    };

    enum class State {
        INIT,
        HEADERS,
        EXTRACT,
        HANDLE,
        RESPOND
    } state = State::INIT;
    Writer* writer;
    Reader* reader;
    union {
        struct {
            HttpRequestStatusLine statusLine;
        } init;

        ReadHttpHeaders<SizedBuffer<MAX_HEADER_NAME>,
                        SizedBuffer<MAX_HEADER_VALUE>, HeaderVisitor>
            readHeaders;

        struct {
            HttpRequest request;
            char extractor;
            Union<typename http_extractor<Extractors>::ExtractRequestFuture...>
                future;
        } extractFutures;

        HandleFuture handleFuture;
        typename http_response<Response>::RespondFuture respondFuture;
    };

    char currentExtractor = 0;
    union {
        Tuple<Extractors...> extractors;
    };
};

// http_extractor/response implementations

template <>
struct http_response<const char*> {
    class RespondFuture : Future<RespondFuture, void_> {
       public:
        RespondFuture(Writer* writer, const char* response)
            : init{writer, response} {}

        Poll<void_> poll() {
            switch (state) {
                case State::INIT: {
                    state = State::WRITE;
                    writeFromBuffer = init.writer->writeFromBuffer(
                        init.response, stringLength(init.response));
                }
                case State::WRITE: {
                    AWAIT_PTR(writeFromBuffer, result)
                    if (result != writeFromBuffer->getBufferLength()) {
                        READY(void_())
                    }
                    READY(void_())
                }
            }
            return Poll<void_>::pending();
        }

       private:
        enum class State { INIT, WRITE } state = State::INIT;
        union {
            struct {
                Writer* writer;
                const char* response;
            } init;
            WriteFromBuffer* writeFromBuffer;
        };
    };

    static RespondFuture respond(Writer* writer, const char* response) {
        return RespondFuture(writer, response);
    }
};

struct HttpBodyReader {
    Reader* bodyReader;
};

template <>
struct http_extractor<HttpBodyReader> {
    static constexpr const char* ERROR_RESPONSE =
        "HTTP/1.1 500 Internal Server Error\r\nContent-Length: "
        "41\r\n\r\nAnother "
        "extractor already extracted body!";

    static HttpBodyReader createExtractor() {
        return HttpBodyReader{.bodyReader = nullptr};
    }

    static void extractStatusLine(HttpBodyReader* extractor,
                                  HttpRequestStatusLine statusLine) {}

    static constexpr const size_t MAX_HEADER_NAME = 0;
    static constexpr const size_t MAX_HEADER_VALUE = 0;

    static void extractHeader(HttpBodyReader* extractor, BufferRef name,
                              BufferRef value) {}

    class ExtractRequestFuture : Future<ExtractRequestFuture, void_> {
       public:
        ExtractRequestFuture(HttpBodyReader* extractor, HttpRequest* request)
            : extractor(extractor), request(request) {}

        Poll<void_> poll() {
            switch (state) {
                case State::INIT: {
                    auto bodyOpt = request->tryTakeBody();
                    if (bodyOpt.isEmpty()) {
                        goto initWriteErr;
                    }

                    extractor->bodyReader = bodyOpt.get();
                    READY(void_())
                }
                initWriteErr : {
                    Writer* writer = request->writeResponse();
                    BufferRef err = BufferRef(ERROR_RESPONSE);
                    writeFromBuffer =
                        writer->writeFromBuffer(err.data, err.length);
                }
                case State::WRITE_ERR: {
                    AWAIT_PTR(writeFromBuffer, result)
                    READY(void_())
                }
            }
            return Poll<void_>::pending();
        }

       private:
        enum class State { INIT, WRITE_ERR } state = State::INIT;
        HttpBodyReader* extractor;
        union {
            HttpRequest* request;
            WriteFromBuffer* writeFromBuffer;
        };
    };

    static ExtractRequestFuture extractRequest(HttpBodyReader* extractor,
                                               HttpRequest* request) {
        return ExtractRequestFuture(extractor, request);
    }
};

struct StatusCodeResponse {
    HttpVersion httpVersion;
    unsigned short code;
    BufferRef reason = BufferRef("idk");
};

template <>
struct http_response<StatusCodeResponse> {
    class RespondFuture : Future<RespondFuture, void_> {
       public:
        RespondFuture(Writer* writer, StatusCodeResponse response)
            : init{writer, response} {}

        Poll<void_> poll() {
            switch (state) {
                case State::INIT: {
                    Writer* writer = init.writer;
                    StatusCodeResponse response = init.response;

                    INIT_AWAIT(WRITE_RESPONSE_STATUS_LINE,
                               writeHttpResponseStatusLine,
                               WriteHttpResponseStatusLine(
                                   writer,
                                   {.httpVersion = response.httpVersion,
                                    .code = response.code},
                                   response.reason),
                               result)
                    if (!result) {
                        READY(void_())
                    }
                    READY(void_())
                }
            }
            return Poll<void_>::pending();
        }

       private:
        enum class State {
            INIT,
            WRITE_RESPONSE_STATUS_LINE
        } state = State::INIT;
        union {
            struct {
                Writer* writer;
                StatusCodeResponse response;
            } init;
            WriteHttpResponseStatusLine writeHttpResponseStatusLine;
        };
    };

    static RespondFuture respond(Writer* writer, StatusCodeResponse response) {
        return RespondFuture(writer, response);
    }
};

template <typename T>
struct HttpJsonBody {
    HttpJsonBody() {}
    HttpJsonBody(T value) : value(value) {}

    Optional<int> contentLength = Optional<int>::empty();
    union {
        T value;
    };
};

template <typename T>
struct http_extractor<HttpJsonBody<T>> {
    static HttpJsonBody<T> createExtractor() { return HttpJsonBody<T>(); }

    static void extractStatusLine(HttpJsonBody<T>* extractor,
                                  HttpRequestStatusLine statusLine) {}

    static const size_t MAX_HEADER_NAME =
        template_utils::const_str_length("Content-Length");
    static const size_t MAX_HEADER_VALUE = 4;

    // TODO: Check for application/json header value

    static void extractHeader(HttpJsonBody<T>* extractor, BufferRef headerName,
                              BufferRef headerValue) {
        if (!headerName.equalsIgnoreCase("Content-Length")) {
            return;
        }
        Optional<double> contentLengthOpt = readDoubleFromBuffer(headerValue);
        if (contentLengthOpt.isEmpty()) {
            return;
        }
        double contentLength = contentLengthOpt.get();

        extractor->contentLength = Optional<int>::of((int)contentLength);

        return;
    }

    class ExtractRequestFuture : Future<ExtractRequestFuture, void_> {
       public:
        ExtractRequestFuture(HttpRequest* request, HttpJsonBody<T>* extractor)
            : request(request), extractor(extractor) {}

        Poll<void_> poll() {
            switch (state) {
                case State::INIT: {
                    if (extractor->contentLength.isEmpty()) {
                        goto initWriteErrInvalidJson;
                    }

                    Optional<Reader*> readerOpt = request->tryTakeBody();
                    if (readerOpt.isEmpty()) {
                        goto initWriteErrBodyTaken;
                    }
                    reader = readerOpt.get();

                    INIT_AWAIT(READ_JSON, deserializeJson,
                               DeserializeJson<T>(reader), result)
                    if (result.isEmpty()) {
                        goto initWriteErrInvalidJson;
                    }

                    this->extractor->value = result.get();

                    READY(void_())
                }
                initWriteErrBodyTaken : {
                    writer = request->writeResponse();
                    state = State::WRITE_ERR;

                    BufferRef err = BufferRef(ERROR_RESPONSE_BODY_TAKEN);
                    writeFromBuffer =
                        writer->writeFromBuffer(err.data, err.length);
                    goto pollWriteErr;
                }
                initWriteErrInvalidJson : {
                    writer = request->writeResponse();
                    state = State::WRITE_ERR;

                    BufferRef err = BufferRef(ERROR_RESPONSE_INVALID_JSON);
                    writeFromBuffer =
                        writer->writeFromBuffer(err.data, err.length);
                    goto pollWriteErr;
                }
                pollWriteErr:
                case State::WRITE_ERR: {
                    Poll<typename template_utils ::future_output<
                        decltype(writeFromBuffer)>::type>
                        poll;
                    poll = writeFromBuffer->poll();
                    if (poll.isPending()) {
                        return Poll<typename template_utils ::future_output<
                            decltype(this)>::type>::pending();
                    }
                    typename template_utils ::future_output<
                        decltype(writeFromBuffer)>::type result = poll.get();
                    READY(void_())
                }
            }
            return Poll<void_>::pending();
        }

       private:
        static constexpr const char* ERROR_RESPONSE_BODY_TAKEN =
            "HTTP/1.1 500 Internal Server Error\r\nContent-Length: "
            "41\r\n\r\nAnother extractor already extracted body!";
        static constexpr const char* ERROR_RESPONSE_INVALID_JSON =
            "HTTP/1.1 400 Bad Request\r\nContent-Length: "
            "13\r\n\r\nInvalid Json!";

        enum class State { INIT, READ_JSON, WRITE_ERR } state = State::INIT;
        HttpJsonBody<T>* extractor;
        HttpRequest* request;
        union {
            Reader* reader;
            Writer* writer;
        };
        union {
            DeserializeJson<T> deserializeJson;
            WriteFromBuffer* writeFromBuffer;
        };
    };

    static ExtractRequestFuture extractRequest(HttpJsonBody<T>* extractor,
                                               HttpRequest* request) {
        return ExtractRequestFuture(request, extractor);
    }
};

template <typename T>
struct http_response<HttpJsonBody<T>> {
    class RespondFuture : Future<RespondFuture, void_> {
       private:
        static constexpr const char* HEADER_JSON =
            "Content-Type: application/json\r\n\r\n";

        BufferRef getHeaderJson() { return BufferRef(HEADER_JSON); }

       public:
        RespondFuture(Writer* writer, T value) : writer(writer), value(value) {}

        Poll<void_> poll() {
            switch (state) {
                case State::INIT: {
                    // TODO:  Use transfer encoding so
                    // we don't need to know the size of the response now
                    auto future = WriteHttpResponseStatusLine(
                        writer,
                        HttpResponseStatusLine{
                            .httpVersion = HttpVersion::HTTP_1_1,
                            .code = 200,
                        },
                        BufferRef("Ok"));
                    INIT_AWAIT(WRITE_RESPONSE_LINE, writeResponseLine, future,
                               result)
                    if (!result) {
                        READY(void_())
                    }

                    BufferRef jsonHeader = getHeaderJson();
                    writeFromBuffer = writer->writeFromBuffer(
                        jsonHeader.data, jsonHeader.length);
                }
                case State::WRITE_JSON_HEADER: {
                    AWAIT_PTR(writeFromBuffer, result)
                    if (result != getHeaderJson().length) {
                        READY(void_())
                    }

                    INIT_AWAIT(WRITE_JSON, serializeJson,
                               SerializeJson<T>(writer, &value), result)
                    if (!result) {
                        READY(void_())
                    }
                    READY(void_())
                }
            }
            return Poll<void_>::pending();
        }

       private:
        enum class State {
            INIT,
            WRITE_RESPONSE_LINE,
            WRITE_JSON_HEADER,
            WRITE_JSON
        } state = State::INIT;
        // TODO: use writer in union
        Writer* writer;
        T value;
        union {
            WriteFromBuffer* writeFromBuffer;
            WriteHttpResponseStatusLine writeResponseLine;
            SerializeJson<T> serializeJson;
        };
    };

    static RespondFuture respond(Writer* writer, HttpJsonBody<T> response) {
        return RespondFuture(writer, response.value);
    }
};

struct HttpBodyResponse {
    BufferRef body;
};

template <>
struct http_response<HttpBodyResponse> {
    class RespondFuture : Future<RespondFuture, void_> {
       private:
        static constexpr const char* HEADER_1 =
            "HTTP/1.1 200 Ok\r\nContent-Length:";
        static constexpr const char* HEADER_2 = "\r\n\r\n";

       public:
        RespondFuture(Writer* writer, HttpBodyResponse response)
            : writer(writer), response(response) {}

        Poll<void_> poll() {
            switch (state) {
                case State::INIT: {
                    state = State::WRITE_HEADERS_1;
                    BufferRef buffer = BufferRef(HEADER_1);
                    writeFromBuffer =
                        writer->writeFromBuffer(buffer.data, buffer.length);
                }
                case State::WRITE_HEADERS_1: {
                    AWAIT_PTR(writeFromBuffer, result)
                    if (result != BufferRef(HEADER_1).length) {
                        READY(void_())
                    }

                    state = State::WRITE_CONTENT_LENTH;
                    writeDouble =
                        WriteDouble<10>(writer, (double)(response.body.length));
                }
                case State::WRITE_CONTENT_LENTH: {
                    AWAIT(writeDouble, result)
                    if (!result) {
                        READY(void_())
                    }

                    state = State::WRITE_HEADERS_2;
                    BufferRef buffer = BufferRef(HEADER_2);
                    writeFromBuffer =
                        writer->writeFromBuffer(buffer.data, buffer.length);
                }
                case State::WRITE_HEADERS_2: {
                    AWAIT_PTR(writeFromBuffer, result)
                    if (result != BufferRef(HEADER_2).length) {
                        READY(void_())
                    }

                    state = State::WRITE_CONTENT;
                    BufferRef buffer = response.body;
                    writeFromBuffer =
                        writer->writeFromBuffer(buffer.data, buffer.length);
                }
                case State::WRITE_CONTENT: {
                    AWAIT_PTR(writeFromBuffer, result)
                    if (result != response.body.length) {
                        READY(void_())
                    }
                    READY(void_())
                }
            }
            return Poll<void_>::pending();
        }

       private:
        enum class State {
            INIT,
            WRITE_HEADERS_1,
            WRITE_CONTENT_LENTH,
            WRITE_HEADERS_2,
            WRITE_CONTENT
        } state = State::INIT;  // TODO: optimize
        Writer* writer;
        HttpBodyResponse response;
        union {
            WriteFromBuffer* writeFromBuffer;
            WriteDouble<10> writeDouble;
        };
    };

    static RespondFuture respond(Writer* writer, HttpBodyResponse response) {
        return RespondFuture(writer, response);
    }
};

#endif