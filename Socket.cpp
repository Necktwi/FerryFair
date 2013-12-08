
// Implementation of the Socket class.


#include "Socket.h"
#include "debug.h"
#include <string.h>
#include <string>
#include <errno.h>
#include <fcntl.h>
#include<iostream>
#include <openssl/bio.h> 
#include <openssl/ssl.h> 
#include <openssl/err.h> 

std::string CA_FILE = "certs/ferryfair.cert";
std::string CLIENT_KEY = "certs/ferryport.ferryfair.key";
std::string CLIENT_CERT = "certs/ferryport.ferryfair.cert";

void Socket::InitializeSSL() {
    SSL_load_error_strings();
    SSL_library_init();
    OpenSSL_add_all_algorithms();
}

void Socket::DestroySSL() {
    ERR_free_strings();
    EVP_cleanup();
}

void Socket::ShutdownSSL(SSL* ssl) {
    SSL_shutdown(ssl);
    SSL_free(ssl);
}

Socket::Socket() :
m_sock(-1), socketType(DEFAULT) {
    memset(&m_addr,
            0,
            sizeof ( m_addr));
}

Socket::Socket(SOCKET_TYPE socketType, std::string trustedCA, std::string privatecert, std::string privatekey) :
m_sock(-1), socketType(socketType), trustedCA(trustedCA), privatecert(privatecert), privatekey(privatekey) {
    memset(&m_addr, 0, sizeof ( m_addr));
}

Socket::~Socket() {
    if (is_valid())::close(m_sock);
    if (socketType == Socket::TLS1_1) {
        ShutdownSSL(cSSL);
        DestroySSL();
    }
}

bool Socket::create(int timeout_sec) {
    m_sock = socket(AF_INET,
            SOCK_STREAM,
            0);

    if (!is_valid())
        return false;


    // TIME_WAIT - argh
    int on = 1;
    if (setsockopt(m_sock, SOL_SOCKET, SO_REUSEADDR, (const char*) &on, sizeof ( on)) == -1)
        return false;

    struct timeval timeout;
    timeout.tv_sec = timeout_sec;
    timeout.tv_usec = 0;

    if (setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, (char *) &timeout, sizeof (timeout)) < 0)
        return false;

    if (setsockopt(m_sock, SOL_SOCKET, SO_SNDTIMEO, (char *) &timeout, sizeof (timeout)) < 0)
        return false;

    if (socketType == Socket::TLS1_1) {
        InitializeSSL();
        sslctx = SSL_CTX_new(TLSv1_1_method());
        SSL_CTX_set_options(sslctx, SSL_OP_SINGLE_DH_USE);
        /*----- Load a client certificate into the SSL_CTX structure -----*/
        if (SSL_CTX_use_certificate_file(sslctx, (char*) CLIENT_CERT.c_str(), SSL_FILETYPE_PEM) <= 0) {
            ERR_print_errors_fp(stderr);
            exit(1);
        }

        /*----- Load a private-key into the SSL_CTX structure -----*/
        if (SSL_CTX_use_PrivateKey_file(sslctx, (char*) CLIENT_KEY.c_str(), SSL_FILETYPE_PEM) <= 0) {
            ERR_print_errors_fp(stderr);
            exit(1);
        }
        int CA = SSL_CTX_load_verify_locations(sslctx, (char*) CA_FILE.c_str(), NULL);
        SSL_CTX_set_verify(sslctx, SSL_VERIFY_PEER, NULL);
        SSL_CTX_set_verify_depth(sslctx, 1);
        cSSL = SSL_new(sslctx);
        int sssfd = SSL_set_fd(cSSL, m_sock);
    }
    return true;

}

bool Socket::bind(const int port) {
    if (!is_valid()) {
        return false;
    }

    m_addr.sin_family = AF_INET;
    m_addr.sin_addr.s_addr = INADDR_ANY;
    m_addr.sin_port = htons(port);

    int bind_return = ::bind(m_sock, (struct sockaddr *) &m_addr, sizeof ( m_addr));
    if (bind_return == -1) {
        return false;
    }
    return true;
}

bool Socket::listen() const {
    if (!is_valid()) {
        return false;
    }

    int listen_return = ::listen(m_sock, MAXCONNECTIONS);


    if (listen_return == -1) {
        return false;
    }

    return true;
}

int Socket::accept() const {
    int addr_length = sizeof ( m_addr);
    int sock = ::accept(m_sock, (sockaddr *) & m_addr, (socklen_t *) & addr_length);

    if (sock <= 0) {
        return -1;
    } else {
        if (socketType != DEFAULT) {
            SSL_accept(cSSL);
        }
        return sock;
    }
}

int Socket::accept(sockaddr* s_addr, SSL* cssl, SOCKET_TYPE socketType) const {
    int addr_length = sizeof (*s_addr);
    int sock = ::accept(m_sock, s_addr, (socklen_t *) & addr_length);
    if (sock <= 0) {
        return -1;
    } else {
        if (socketType != DEFAULT) {
            SSL_accept(cssl);
        }
        return sock;
    }
}

bool Socket::send(const std::string s, int __flags) const {
    int status;
    if (socketType != DEFAULT) {
        status = SSL_write(cSSL, s.c_str(), s.size());
    } else {
        status = ::send(m_sock, s.c_str(), s.size(), MSG_NOSIGNAL | __flags);
    }
    if (status == -1) {
        return false;
    } else {
        return true;
    }
}

bool Socket::send(const std::string* s, int __flags) const {
    int status;
    if (socketType != DEFAULT) {
        status = SSL_write(cSSL, s->c_str(), s->size());
    } else {
        status = ::send(m_sock, s->c_str(), s->size(), MSG_NOSIGNAL | __flags);
    }
    if (status == -1) {
        return false;
    } else {
        return true;
    }
}

bool Socket::send(const std::string s) const {
    int status;
    if (socketType != DEFAULT) {
        status = SSL_write(cSSL, s.c_str(), s.size());
    } else {
        status = ::send(m_sock, s.c_str(), s.size(), MSG_NOSIGNAL);
    }
    if (status == -1) {
        return false;
    } else {
        return true;
    }
}

int Socket::recv(std::string& s, int size) const {
    char buf [ size + 1 ];

    s = "";

    memset(buf, 0, size + 1);

    int status;
    if (socketType != DEFAULT) {
        status = SSL_read(cSSL, (char *) buf, size);
    } else {
        status = ::recv(m_sock, buf, size, 0);
    }
    if (status == -1) {
        std::cout << "status == -1   errno == " << errno << "  in Socket::recv\n";
        return 0;
    } else if (status == 0) {
        return 0;
    } else {
        s = buf;
        return status;
    }
}

bool Socket::connect(const std::string host, const int port) {
    if (!is_valid()) return false;

    m_addr.sin_family = AF_INET;
    m_addr.sin_port = htons(port);
    struct hostent* hent;
    struct in_addr **addr_list;
    hent = gethostbyname(host.c_str());
    addr_list = (struct in_addr **) hent->h_addr_list;
    if ((debug & 4) == 4) {
        int i = 0;
        std::cout << "\n" + getTime() + " Socket::connect gethostbyname : ";
        while (addr_list[i] != NULL) {
            printf("%s ", inet_ntoa(*addr_list[i]));
            i++;
        }
        std::cout << "\n";
        fflush(stdout);
    }
    int status = inet_pton(AF_INET, inet_ntoa(*addr_list[0]), &m_addr.sin_addr);

    if (errno == EAFNOSUPPORT) return false;

    status = ::connect(m_sock, (sockaddr *) & m_addr, sizeof ( m_addr));

    if (status == 0) {
        if (socketType != DEFAULT) {
            int err = SSL_connect(cSSL);
            if (SSL_get_peer_certificate(cSSL) != NULL) {
                if (SSL_get_verify_result(cSSL) == X509_V_OK) {
                    return true;
                }
            }
            if (err <= 0) {
                //log and close down ssl
                int sslerr = SSL_get_error(cSSL, err);
                ShutdownSSL(cSSL);
            }
        }
        return true;
    } else {
        return false;
    }
}

void Socket::set_non_blocking(const bool b) {
    int opts;
    opts = fcntl(m_sock, F_GETFL);
    if (opts < 0) {
        return;
    }
    if (b)
        opts = (opts | O_NONBLOCK);
    else
        opts = (opts & ~O_NONBLOCK);

    fcntl(m_sock, F_SETFL, opts);
}