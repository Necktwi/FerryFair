// Definition of the Socket class

#ifndef SOCKET_H
#define SOCKET_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <string>
#include <arpa/inet.h>
#include <openssl/ossl_typ.h>


const int MAXHOSTNAME = 200;
const int MAXCONNECTIONS = 5;
const int DMAXRECV = 500;

class Socket {
public:

    enum SOCKET_TYPE {
        DEFAULT, TLS1_1
    };

    bool is_valid() const {
        return m_sock != -1;
    }
    int MAXRECV = 500;
    Socket();
    Socket(SOCKET_TYPE socketType, std::string trustedCA, std::string privatecert, std::string privatekey);
    virtual ~Socket();
    bool create(int timeout_sec = 10);
    bool bind(const int port);
    bool listen() const;
    int accept() const;
    int accept(sockaddr* s_addr, SSL* cssl, SOCKET_TYPE socketType) const;
    bool connect(const std::string host, const int port);
    bool send(const std::string s, int __flags) const;
    bool send(const std::string* s, int __flags) const;
    bool send(const std::string s) const;
    int recv(std::string&, int size = DMAXRECV) const;
    void set_non_blocking(const bool);
    static void InitializeSSL();
    static void DestroySSL();
    static void ShutdownSSL(SSL* ssl);

protected:
    int m_sock = -1;
    sockaddr_in m_addr;
    SSL_CTX *sslctx;
    SSL *cSSL;
    std::string trustedCA;
    std::string privatecert;
    std::string privatekey;
    SOCKET_TYPE socketType;
};

#endif