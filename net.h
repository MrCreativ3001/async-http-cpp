#ifndef CPP_ASYNC_HTTP_NET_H
#define CPP_ASYNC_HTTP_NET_H

#include "reader.h"
#include "utils.h"
#include "writer.h"

namespace net {

// How Client/Server should be implemented
class Client {
    Optional<Writer*> getWriter() = delete;
    Optional<Reader*> getReader() = delete;

    void close() = delete;
    bool isClosed() = delete;
};

class Server {
    typedef Client Client;

    // Returning empty means that the connection pool is full or the server is
    // closed(maybe because of an error).
    typedef Future<void_, Optional<Tuple<size_t, Client*>>> AcceptFuture;
    AcceptFuture accept() = delete;

    void freeClient(size_t clientId) = delete;

    void close() = delete;
    bool isClosed() = delete;
};

}  // namespace net

#endif