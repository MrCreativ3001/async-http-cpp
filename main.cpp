// Includes required for library to work with gcc
using namespace std;
#include <cmath>
#include <iostream>

#include "stddef.h"

// Library
#include "buffer.h"
#include "deser.h"
#include "http.h"
#include "http_handler.h"
#include "json.h"
#include "net.h"
#include "ser.h"

// Windows
#include "integration_win.h"

void clearStack() { char array[10000] = {0}; }

REFLECTION_STRUCT(PersonId, (SizedBuffer<20>)(name)(int)(id))
REFLECTION_STRUCT(Person, (PersonId)(id)(int)(age)(bool)(playsFortnite))

void testReflection() {}

void testSerialize() {
    std::cout << std::endl << "Test json Serialize" << std::endl;

    SizedBuffer<1000> buffer = SizedBuffer<1000>();
    BufferWriter writer = writeToBuffer(buffer.asFullRef());

    Person person = {.id =
                         {
                             .name = SizedBuffer<20>(BufferRef("Radiant")),
                             .id = 0,
                         },
                     .age = 16,
                     .playsFortnite = false};

    auto future = SerializeJson<Person>(&writer, &person);

    bool result = blockOn(future);
    std::cout << "Result: " << result << ", JSON: "
              << writer.getImpl()->getFilledBuffer().copyToCString()
              << std::endl;
}

void testDeserialize() {
    std::cout << std::endl << "Test json Deserialize" << std::endl;

    BufferRef buffer = BufferRef(R"(
        {
        "id": {"name":"Test","id":10},
        "age": 10,
        "playsFortnite": false
        }
        )");
    BufferReader reader = readFromBuffer(buffer);

    auto future = DeserializeJson<Person>(&reader);

    auto result = blockOn(future);
    if (result.isEmpty()) {
        std::cout << "Deserializing Failed" << std::endl;
        return;
    }
    Person person = result.get();
    std::cout << "Result: Id: " << person.id.id
              << ", Name: " << person.id.name.asRef().copyToCString()
              << ", Age:" << person.age << ", playsFortnite: "
              << (person.playsFortnite ? "true" : "false") << std::endl;
}

struct TestHandler {};

template <>
struct http_handler<TestHandler> {
    typedef template_utils::pack<HttpJsonBody<PersonId>> Extractors;
    // Must implement http_response
    typedef HttpJsonBody<PersonId> Response;

    typedef Instant<Response> HandleFuture;

    static HandleFuture handle(HttpJsonBody<PersonId> person) {
        return Instant<HttpJsonBody<PersonId>>(person);
    };
};

void testHttpHandler() {
    std::cout << std::endl << "Test Http handler" << std::endl;

    HttpRequestStatusLine statusLine = {
        .version = HttpVersion::HTTP_2_0,
        .method = HttpMethod::GET,
    };

    BufferRef readRef = BufferRef(
        "header: value\r\n"
        "header2: value \r\n"
        "Content-Length:100\r\n"  // Content-Length is required but does
                                  // nothing when parsing json
        "\r\n"
        R"({"name":"Radiant","id":10})");
    BufferReader reader = readFromBuffer(readRef);

    SizedBuffer<1000> write;
    BufferRef writeRef = write.asFullRef();
    BufferWriter writer = writeToBuffer(writeRef);

    HandleHttpRequest<TestHandler> handle =
        HandleHttpRequest<TestHandler>(statusLine, &reader, &writer);

    blockOn(handle);

    std::cout << "Http Request: " << std::endl
              << readRef.copyToCString() << std::endl
              << "Http Response: " << std::endl
              << writer.getImpl()->getFilledBuffer().copyToCString() << "\n";
}

void startHttpServer() {
    using namespace integration_win;

    std::cout << "hosting server on port 8000. This will echo the JSON struct PersonId in TestHandler for a post request." << std::endl;

    SimpleWinServer<10> server = SimpleWinServer<10>(8000);

    while (true) {
        auto result = blockOn(server.accept());
        if (result.isEmpty()) {
            break;
        }
        size_t clientId = result.get().template at<0>();
        WinClient* client = result.get().template at<1>();
        Reader* reader = client->getReader().get();
        Writer* writer = client->getWriter().get();

        SizedBuffer<20> pathStore;
        auto statusLineOpt = blockOn(
            ReadHttpRequestStatusLine<SizedBuffer<20>>(reader, &pathStore));
        if (statusLineOpt.isEmpty()) {
            server.freeClient(clientId);
            continue;
        }
        HttpRequestStatusLine statusLine = statusLineOpt.get();

        blockOn(HandleHttpRequest<TestHandler>(statusLine, reader, writer));

        server.freeClient(clientId);
    }

    std::cout << "Closing Server" << std::endl;
}

int main() {
    std::cout << std::endl << "Size of Classes/Structs:" << std::endl;

    std::cout << "size_t: " << sizeof(size_t) << std::endl;
    std::cout << "BufferReader: " << sizeof(BufferReader) << std::endl;

    std::cout << "BufferWriter: " << sizeof(BufferWriter) << std::endl;
    std::cout << "WritePersonJson: " << sizeof(SerializeJson<PersonId>)
              << std::endl;

    std::cout << "ReadPersonJson: " << sizeof(DeserializeJson<Person>)
              << std::endl;

    std::cout << "HandleRequest: " << sizeof(HandleHttpRequest<TestHandler>)
              << std::endl;

    testReflection();
    testSerialize();
    testDeserialize();
    testHttpHandler();

    startHttpServer();

    return 0;
}

// TODO: implement readfuture/writefuture for much more classes.
// TODO: optimize by using readfuture/writefuture's getReader/getWriter