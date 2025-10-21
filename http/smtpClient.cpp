// smtp_all_style.cpp
// SMTP client supporting plain, STARTTLS, and implicit TLS.
// Build: g++ -std=c++17 smtp_all_style.cpp -o smtp_all -lssl -lcrypto

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "smtpClient.h"
#include "https.h"

static void print_ssl_error (const char *msg) {
   unsigned long e = ERR_get_error();
   if (e) {
      char buf[256];
      ERR_error_string_n(e, buf, sizeof(buf));
      cerr << msg << ": " << buf << "\n";
   }
}

static string b64_encode (const string &in) {
   static const char *tbl =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   string out;
   int val = 0, valb = -6;
   for (unsigned char c : in) {
      val = (val << 8) + c;
      valb += 8;
      while (valb >= 0) {
         out.push_back(tbl[(val >> valb) & 0x3F]);
         valb -= 6;
      }
   }
   if (valb > -6) out.push_back(tbl[((val << 8) >> (valb + 8)) & 0x3F]);
   while (out.size() % 4) out.push_back('=');
   return out;
}

class TCPClient {
   int sockfd_{ -1 };
public:
   ~TCPClient () { if (sockfd_ >= 0) ::close(sockfd_); }

   bool connect_host (const string &host, int port) {
      addrinfo hints{}, *res = nullptr;
      hints.ai_family = AF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;
      if (getaddrinfo(host.c_str(), to_string(port).c_str(), &hints, &res)
          != 0) {
         perror("getaddrinfo");
         return false;
      }
      for (addrinfo *p = res; p; p = p->ai_next) {
         sockfd_ = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
         if (sockfd_ < 0) continue;
         if (connect(sockfd_, p->ai_addr, p->ai_addrlen) == 0) {
            freeaddrinfo(res);
            return true;
         }
         ::close(sockfd_);
         sockfd_ = -1;
      }
      freeaddrinfo(res);
      perror("connect");
      return false;
   }

   int fd () const { return sockfd_; }
   ssize_t send_raw (const char *buf, size_t len) { return ::send(sockfd_, buf, len, 0); }
   ssize_t recv_raw (char *buf, size_t len) { return ::recv(sockfd_, buf, len, 0); }
};

class SmtpClient {
   TCPClient tcp_;
   SSL_CTX *ctx_{ nullptr };
   SSL *ssl_{ nullptr };
   bool use_ssl_{ false };

public:
   SmtpClient () {
      SSL_library_init();
      SSL_load_error_strings();
      OpenSSL_add_all_algorithms();
   }

   ~SmtpClient () {
      if (ssl_) { SSL_shutdown(ssl_); SSL_free(ssl_); }
      if (ctx_) SSL_CTX_free(ctx_);
   }

   bool connect_plain (const string &host, int port) {
      return tcp_.connect_host(host, port);
   }

   bool upgrade_to_tls (const string &host) {
      ctx_ = SSL_CTX_new(SSLv23_client_method());
      if (!ctx_) return false;
      ssl_ = SSL_new(ctx_);
      SSL_set_fd(ssl_, tcp_.fd());
      SSL_set_tlsext_host_name(ssl_, host.c_str());
      if (SSL_connect(ssl_) != 1) {
         print_ssl_error("SSL_connect");
         return false;
      }
      use_ssl_ = true;
      return true;
   }

   bool send_line (const string &line) {
      string msg = line;
      if (msg.back() != '\n') msg += "\r\n";
      int n = use_ssl_ ? SSL_write(ssl_, msg.data(), msg.size())
                       : tcp_.send_raw(msg.c_str(), msg.size());
      return n > 0;
   }

   bool read_response (string &out) {
      char buf[4096];
      out.clear();
      string acc;
      for (;;) {
         ssize_t n = use_ssl_ ? SSL_read(ssl_, buf, sizeof(buf))
                              : tcp_.recv_raw(buf, sizeof(buf));
         if (n <= 0) break;
         acc.append(buf, buf + n);
         if (acc.size() >= 5 && acc[acc.size()-2] == '\r'
                             && acc[acc.size()-1] == '\n') {
            istringstream ss(acc);
            string line, last;
            while (getline(ss, line)) {
               if (!line.empty() && line.back() == '\r')
                  line.pop_back();
               last = line;
            }
            if (last.size() > 3 && last[3] == ' ') {
               out = acc;
               return true;
            }
         }
      }
      return !out.empty();
   }
};

static bool expect_code (const string &resp, const string &code) {
   return resp.rfind(code, 0) == 0 ||
          resp.find("\n" + code + " ") != string::npos;
}

int sendMail (
   string host, int port, string mode, string user, string pass, string from,
   string to, string subj, string body
) {
   ffl_debug(SM, "%s, %d, %s, %s, %s, %s, %s, %s, %s", host.c_str(), port,
             mode.c_str(), user.c_str(), pass.c_str(), from.c_str(), to.c_str(),
             subj.c_str(), body.c_str());
   SmtpClient client;
   if (!client.connect_plain(host, port)) {
      cerr << "Cannot connect.\n"; return 2;
   }
   
   string resp;
   client.read_response(resp);
   cout << resp;

   client.send_line("EHLO localhost");
   client.read_response(resp);
   cout << resp;

   if (mode == "starttls") {
      client.send_line("STARTTLS");
      client.read_response(resp);
      cout << resp;
      if (!expect_code(resp, "220")) {
         cerr << "STARTTLS failed\n"; return 3;
      }
      if (!client.upgrade_to_tls(host)) {
         cerr << "TLS handshake failed\n"; return 4;
      }
      client.send_line("EHLO localhost");
      client.read_response(resp);
      cout << resp;
   } else if (mode == "tls") {
      if (!client.upgrade_to_tls(host)) {
         cerr << "TLS connect failed\n"; return 5;
      }
      client.read_response(resp);
      cout << resp;
      client.send_line("EHLO localhost");
      client.read_response(resp);
      cout << resp;
   } else if (mode == "plain") {
      cout << "[!] Unencrypted mode: data sent in cleartext.\n";
   } else {
      cerr << "Unknown mode.\n"; return 6;
   }

   client.send_line("AUTH LOGIN");
   client.read_response(resp); cout << resp;
   client.send_line(b64_encode(user));
   client.read_response(resp); cout << resp;
   client.send_line(b64_encode(pass));
   client.read_response(resp); cout << resp;

   client.send_line("MAIL FROM:<" + from + ">");
   client.read_response(resp); cout << resp;
   client.send_line("RCPT TO:<" + to + ">");
   client.read_response(resp); cout << resp;
   client.send_line("DATA");
   client.read_response(resp); cout << resp;

   ostringstream msg;
   msg << "From: " << from << "\r\n"
       << "To: " << to << "\r\n"
       << "Subject: " << subj << "\r\n"
       << "Content-Type: text/plain; charset=utf-8\r\n\r\n"
       << body << "\r\n.\r\n";
   client.send_line(msg.str());
   client.read_response(resp); cout << resp;

   client.send_line("QUIT");
   client.read_response(resp); cout << resp;
   return 0;
}
