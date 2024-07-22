#ifndef CPP_ASYNC_HTTP_WIN_INTEGRATION_H
#define CPP_ASYNC_HTTP_WIN_INTEGRATION_H

// Win
#undef UNICODE

#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

// Need to link with Ws2_32.lib
#pragma comment(lib, "Ws2_32.lib")
// #pragma comment(lib, "Mswsock.lib")

// Lib
#include "buffer.h"
#include "future.h"
#include "http.h"
#include "http_handler.h"
#include "reader.h"
#include "utils.h"
#include "writer.h"

namespace integration_win {

// Reader
class WinReaderImpl {
   public:
    WinReaderImpl(SOCKET clientSocket) : clientSocket(clientSocket) {}

    Optional<size_t> readIntoBuffer(char *buffer, size_t bufferLength) {
        if (shutdown) {
            return Optional<size_t>::empty();
        }
        if (bufferLength <= 0) {
            return Optional<size_t>::of(0);
        }

        int read = recv(clientSocket, buffer, bufferLength, 0);

        if (read == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                return Optional<size_t>::of(0);
            } else {
                shutdown = true;
                return Optional<size_t>::empty();
            }
        }
        if (read == 0) {
            // Connection closed
            shutdown = true;
            return Optional<size_t>::empty();
        }

        return Optional<size_t>::of((size_t)read);
    }

    bool isShutdown() { return shutdown; }

   private:
    bool shutdown = false;
    SOCKET clientSocket;
};

typedef SimpleReader<WinReaderImpl> WinReader;

WinReader readFromWinSocket(SOCKET clientSocket) {
    return WinReader(WinReaderImpl(clientSocket));
}

class WinWriterImpl {
   public:
    WinWriterImpl(SOCKET clientSocket) : clientSocket(clientSocket) {}

    Optional<size_t> writeFromBuffer(const char *buffer, size_t bufferLength) {
        if (shutdown) {
            return Optional<size_t>::empty();
        }

        int written = send(clientSocket, buffer, bufferLength, 0);

        if (written == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) {
                return Optional<size_t>::of(0);
            } else {
                shutdown = true;
                return Optional<size_t>::empty();
            }
        }
        if (written == 0) {
            // Connection closed
            shutdown = true;
            return Optional<size_t>::empty();
        }

        return Optional<size_t>::of((size_t)written);
    }

   private:
    bool shutdown = false;
    SOCKET clientSocket;
};

typedef SimpleWriter<WinWriterImpl> WinWriter;

WinWriter writeToWinSocket(SOCKET clientSocket) {
    return WinWriter(WinWriterImpl(clientSocket));
}

// Client/Server
class WinClient {
   public:
    explicit WinClient() : WinClient(INVALID_SOCKET) {}
    // The socket must be configurated to be non blocking
    WinClient(SOCKET clientSocket)
        : clientSocket(clientSocket),
          writer(writeToWinSocket(clientSocket)),
          reader(readFromWinSocket(clientSocket)) {}

    Optional<Writer *> getWriter() {
        if (isClosed()) {
            return Optional<Writer *>::empty();
        }
        return Optional<Writer *>::of(&writer);
    }
    Optional<Reader *> getReader() {
        if (isClosed()) {
            return Optional<Reader *>::of(&reader);
        }
        return Optional<Reader *>::of(&reader);
    }

    void close() {
        if (closed) {
            return;
        }

        closed = true;

        if (clientSocket == INVALID_SOCKET) {
            return;
        }

        int iResult = shutdown(clientSocket, SD_SEND);
        if (iResult == SOCKET_ERROR) {
            closesocket(clientSocket);
            return;
        }

        closesocket(clientSocket);
    }
    bool isClosed() { return closed || clientSocket == INVALID_SOCKET; }

   private:
    bool closed = false;
    SOCKET clientSocket;
    WinWriter writer;
    WinReader reader;
};

class SimpleWinClient {
    // TODO
};

SOCKET
acceptSock(SOCKET listenSocket) { return accept(listenSocket, NULL, NULL); };

template <size_t MAX_CONNECTIONS>
class WinServer {
   private:
   public:
    typedef WinClient Client;

    explicit WinServer(SOCKET listenSocket) : listenSocket(listenSocket) {}

    class AcceptFuture : Future<AcceptFuture, Optional<WinClient>> {
       private:
        typedef Optional<Tuple<size_t, WinClient *>> Return;

       public:
        AcceptFuture(WinServer *server) : server(server) {}

        Poll<Return> poll() {
            size_t clientId = MAX_CONNECTIONS;
            for (int i = 0; i < MAX_CONNECTIONS; i++) {
                if (!server->clientsInUse.get(i)) {
                    clientId = i;
                    break;
                }
            }
            if (clientId == MAX_CONNECTIONS) {
                READY(Return::empty())
            }

            // Look if there's a new connection
            int iResult = listen(server->listenSocket, SOMAXCONN);
            if (iResult == SOCKET_ERROR) {
                server->close();
                READY(Return::empty())
            }

            // Accept new connection
            SOCKET clientSocket = acceptSock(server->listenSocket);
            if (clientSocket == INVALID_SOCKET) {
                return Poll<Return>::pending();
            }

            // Set socket to non blocking
            u_long iMode = 1;
            iResult = ioctlsocket(clientSocket, FIONBIO, &iMode);
            if (iResult == SOCKET_ERROR) {
                closesocket(clientSocket);
                return Poll<Return>::pending();
            }

            server->clientsInUse.set(clientId, true);
            WinClient *winClient = &server->clients[clientId];
            *winClient = WinClient(clientSocket);

            READY(Return::of(Tuple<size_t, WinClient *>(clientId, winClient)))
        }

       private:
        WinServer *server;
    };

    AcceptFuture accept() { return AcceptFuture(this); }

    void freeClient(size_t clientId) {
        if (!clientsInUse.get(clientId)) {
            return;
        }
        clientsInUse.set(clientId, false);
        WinClient *client = &clients[clientId];
        client->close();
    }

    void close() {
        if (closed) {
            return;
        }
        closed = true;

        // Close clients
        for (int i = 0; i < MAX_CONNECTIONS; i++) {
            clients[i].close();
        }

        // Close server
        closesocket(listenSocket);
    }
    bool isClosed() { return closed; }

   private:
    bool closed = false;
    SOCKET listenSocket;
    BitSet<MAX_CONNECTIONS> clientsInUse = BitSet<MAX_CONNECTIONS>();
    WinClient clients[MAX_CONNECTIONS];
};

template <size_t MAX_CONNECTIONS>
class SimpleWinServer {
   public:
    SimpleWinServer(int port)
        : server(WinServer<MAX_CONNECTIONS>(INVALID_SOCKET)) {
        WSAData wsaData;
        int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (iResult != 0) {
            return;
        }

        // convert port to string
        static constexpr const size_t PORT_BUF_LEN = 6;
        Optional<SizedBuffer<PORT_BUF_LEN>> portBufOpt =
            writeDoubleToBuffer<10, PORT_BUF_LEN>(port);
        if (portBufOpt.isEmpty()) {
            return;
        }
        SizedBuffer<PORT_BUF_LEN> *portBuf = portBufOpt.getPtr();
        portBuf->data[PORT_BUF_LEN - 1] = '\0';
        char *portStr = portBuf->data;

        // Create address info
        struct addrinfo *result = NULL, *ptr = NULL, hints;
        ZeroMemory(&hints, sizeof(hints));
        // IPV4
        hints.ai_family = AF_INET;
        // stream socket
        hints.ai_socktype = SOCK_STREAM;
        // specify protocol = tcp
        hints.ai_protocol = IPPROTO_TCP;
        // WE want to use the return address to call a bind function
        hints.ai_flags = AI_PASSIVE;

        // Get address
        iResult = getaddrinfo(NULL, portStr, &hints, &result);
        if (iResult != 0) {
            return;
        }

        // Create the socket
        SOCKET listenSocket = INVALID_SOCKET;
        listenSocket =
            socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (listenSocket == INVALID_SOCKET) {
            freeaddrinfo(result);
            return;
        }

        // Bind that socket
        iResult = bind(listenSocket, result->ai_addr, (int)result->ai_addrlen);
        if (iResult != 0) {
            freeaddrinfo(result);
            closesocket(listenSocket);
            return;
        }

        // Free address
        freeaddrinfo(result);

        // Set listenSocket to non blocking
        u_long iMode = 1;
        iResult = ioctlsocket(listenSocket, FIONBIO, &iMode);
        if (iResult == SOCKET_ERROR) {
            closesocket(listenSocket);
            return;
        }

        // create winserver with listenSocket
        server = WinServer<MAX_CONNECTIONS>(listenSocket);
    }

    SimpleWinServer(const SimpleWinServer &other) = delete;
    SimpleWinServer &operator=(const SimpleWinServer &other) = delete;

    SimpleWinServer(SimpleWinServer &&other) : server(other.server) {
        other.server = WinServer<MAX_CONNECTIONS>(INVALID_SOCKET);
    };
    SimpleWinServer &operator=(SimpleWinServer &&other) {
        this->server = other.server;
        other.server = WinServer<MAX_CONNECTIONS>(INVALID_SOCKET);
        return *this;
    }

    ~SimpleWinServer() {
        server.close();
        WSACleanup();
    }

    typedef typename WinServer<MAX_CONNECTIONS>::AcceptFuture AcceptFuture;

    inline AcceptFuture accept() { return server.accept(); }

    inline void freeClient(size_t clientId) { server.freeClient(clientId); }

    inline void close() { server.close(); }
    inline bool isClosed() { return server.isClosed(); }

   private:
    WinServer<MAX_CONNECTIONS> server;
};

}  // namespace integration_win

#endif