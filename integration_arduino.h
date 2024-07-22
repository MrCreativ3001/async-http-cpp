#ifndef CPP_ASYNC_HTTP_ARDUINO_INTEGRATION_H
#define CPP_ASYNC_HTTP_ARDUINO_INTEGRATION_H

// Arduino
#include <WiFi.h>

// Lib
#include "future.h"

// TODO

namespace arduino_integration {

class ArduinoWiFiClient {
   private:
    WiFiClient client;

   public:
    // Client needs to be on non blocking mode
    ArduinoWiFiClient(WiFiClient client) : client(client) {}
};

class ArduinoWiFiServer {
   private:
    WiFiServer server;

   public:
    ArduinoWiFiServer(int port, int max_connections)
        : server(port, max_connections) {
        this->server.setNoDelay(true);
    }

    typedef ArduinoWiFiClient Client;

    class AcceptFuture
        : Future<AcceptFuture, Optional<Tuple<size_t, ArduinoWiFiClient*>>> {};

    AcceptFuture accept() {}

    void freeClient(size_t clientId) {}

    void close() {}
    bool isClosed() {}
};

}  // namespace arduino_integration

#endif
