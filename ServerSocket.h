// Definition of the ServerSocket class

#ifndef SERVERSOCKET_H
#define SERVERSOCKET_H

#include "Socket.h"
#include "SocketException.h"
#include <arpa/inet.h>
#include <errno.h>
#include <openssl/ossl_typ.h>
#include <openssl/ssl.h> 
#include <openssl/err.h> 
#include <iostream>
#include <string>
#include <list>
#include <sys/socket.h>

class ServerSocket : public Socket {
public:

    class Connection : public Socket {
    public:

        Connection(ServerSocket* serverSocket, SSL *cSSL) : serverSocket(serverSocket), cSSL(cSSL) {
            m_sock = serverSocket->Socket::accept(&this->m_addr, & * this->cSSL, serverSocket->socketType);
            if (m_sock <= 0) {
                throw SocketException("Could not respond to new connection");
            }
            //serverSocket->incomingConnections.push_back(*this);
        }

        ~Connection() {
            close(this->m_sock);
        }

        bool operator <<(const std::string& s) const {
            if (!send(s)) {
                throw SocketException("Could not write to socket.");
            }
            return true;
        };

        bool operator >>(std::string& s) const {
            if (!this->recv(s)) {
                throw SocketException("Could not read from socket.");
            }
            return true;
        };

        void *get_in_addr(struct sockaddr *sa) {
            if (sa->sa_family == AF_INET)
                return &(((struct sockaddr_in*) sa)->sin_addr);
            return &(((struct sockaddr_in6*) sa)->sin6_addr);
        }

        std::string getDestinationIP() {
            char s[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET, get_in_addr(&m_addr), s, sizeof s);
            return std::string(s);
        }

        std::string getDestinationPort() {
            return "";
        }

        std::string getSourcePort() {
            return "";
        }
    private:
        int connectionDescriptor = -1;
        SSL *cSSL;
        sockaddr m_addr;
        ServerSocket * serverSocket;
    };
    std::list<Connection> incomingConnections;
    SOCKET_TYPE socketType;

    ServerSocket(int port, SOCKET_TYPE socketType = SOCKET_TYPE::DEFAULT) {
        if (!this->create(0)) {
            throw SocketException("Could not create server socket.");
        }

        if (!this->bind(port)) {
            throw SocketException("Could not bind to port.");
        }

        if (!this->listen()) {
            throw SocketException("Could not listen to socket.");
        }
        this->socketType = socketType;
    };

    ServerSocket() {
    };

    ~ServerSocket() {
    };

    Connection* accept() {
        return new Connection(this, NULL);
    }
private:
};


#endif