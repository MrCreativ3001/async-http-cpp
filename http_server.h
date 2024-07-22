#ifndef CPP_ASYNC_HTTP_HTTP_SERVER_H
#define CPP_ASYNC_HTTP_HTTP_SERVER_H

namespace http_server {

template <typename Server>
class HttpServer {
   public:
    HttpServer(Server *server) : server(server) {}

   private:
    Server *server;
};

}  // namespace http_server

#endif