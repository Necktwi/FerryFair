// mt_server.cpp
// Minimal multithreaded HTTP + HTTPS server with AJAX and form POST support.
// Build:
//   g++ -std=c++17 mt_server.cpp -lssl -lcrypto -lpthread -o mt_server
//
// Generate self-signed cert for testing:
//   openssl req -x509 -newkey rsa:4096 -nodes -keyout key.pem -out cert.pem -days 365 \
//       -subj "/CN=localhost"
//
// Run:
//   ./mt_server --http-port 8080 --https-port 8443 --docroot ./www --cert cert.pem --key key.pem --threads 8
//
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <filesystem>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <set>

#include <FFJSON.h>
#include <logger.h>
#include <myconverters.h>
#include <FerryTimeStamp.h>
#include <mystdlib.h>
#include "https.h"
#include "ferryfair.h"

FFJSON cfg;

int child_exit_status = 0;
FF_LOG_TYPE fflAllowedType = (FF_LOG_TYPE) (FFL_ERR | FFL_NOTICE | FFL_DEBUG |
                                            FFL_INFO);
unsigned int fflAllowedBlks = (uint)HL;

using namespace std;
namespace fs = std::filesystem;

typedef const char* ccp;

void handle_sigint (int) {
   if (!g_running) {
      exit(1);
   }
   g_running = false;
   ffl_debug(HL, "interrupted! g_running: %d", g_running.load());
}

// ---------------- Thread pool ----------------
class ThreadPool {
public:
   ThreadPool (size_t n) {
      start(n);
   }
   ~ThreadPool () {
      stop();
   }

   void enqueue(function<void()> job) {
      {
         unique_lock<mutex> lk(mutex_);
         jobs_.push(move(job));
      }
      cv_.notify_one();
   }

private:
   vector<thread> workers_;
   queue<function<void()>> jobs_;
   mutex mutex_;
   condition_variable cv_;
   bool stopping_ = false;

   void start (size_t n) {
      for (size_t i=0; i<n; ++i) {
         workers_.emplace_back([this] () {
            while (true) {
               function<void()> job;
               {
                  unique_lock<mutex> lk(mutex_);
                  cv_.wait(lk, [this] {
                     return stopping_ || !jobs_.empty();
                  });
                  if (stopping_ && jobs_.empty())
                     return;
                  job = move(jobs_.front());
                  jobs_.pop();
               }
               try {
                  job();
               } catch (const exception &e) {
                  ffl_err(HL, "worker exception: %s", e.what());
               } catch (...) {
                  ffl_err(HL, "worker exception: unknown");
               }
            }
         });
      }
   }

   void stop () {
      {
         unique_lock<mutex> lk(mutex_);
         stopping_ = true;
      }
      cv_.notify_all();
      for(auto &t : workers_)
         if (t.joinable())
            t.join();
   }
};

// ---------------- Utilities ----------------
string url_decode(const string &s) {
   string out;
   out.reserve(s.size());
   for(size_t i=0;i<s.size();++i){
      char c = s[i];
      if (c == '%') {
         if (i + 2 < s.size()) {
            char hex[3] = { s[i+1], s[i+2], 0 };
            char decoded = (char) strtol(hex, nullptr, 16);
            out.push_back(decoded);
            i += 2;
         }
      } else if (c == '+') out.push_back(' ');
      else out.push_back(c);
   }
   return out;
}

// write all bytes to fd
bool isValidMethod (char* buf) {
   static const char* methods = "get post";
   if (strcasestr(methods, buf)) {
      return true;
   }
   return false;
}
int makeNonBlocking (int fd) {
   int flags = fcntl(fd, F_GETFL, 0);
   if (flags == -1) return -1;
   return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

using cread = function<size_t(char*, size_t)>;
void parseCookie (cread read, FFJSON& ffCookie) {
   char c;
   string key,value;
   int retry = 10;
   while (true) {
     readagain:
      ssize_t r = read(&c, 1);
      if (r<=0) {
         ffl_debug(HL, "r: %zu", r);
         if (retry<10) {
            ++retry;
            ffl_debug(HL, "retry %d", retry);
            this_thread::sleep_for(chrono::milliseconds(200));
            ffl_debug(HL, "retry %d woke", retry);
            goto readagain;
         }
         retry=0;
         return;
      }
      string* buf = &key;
      int i;
      switch (c) {
         case '=':
            while (buf->back()==' ')
               buf->pop_back();
            buf = &value;
            break;
         case '\r':
            continue;
         case ';':
         case '\n':
            while (buf->back()==' ')
               buf->pop_back();
            ffCookie[key]=value;
            if (c=='\n')
               return;
            else
               buf=&key;
            break;
         case ' ':
            if (!buf->length())
               continue;
            break;
         default:
            (*buf)+=c;
            break;
      }
   }
}

void parseHTTP (cread read, FFJSON& ffHttp) {
   unsigned int i=0;
   unsigned int pairStartPin=i;
   char c;
   char buf[1024];
   ffl_info(HL, "request: ");
   int li=0;
   int spCnt=0;
   int ci=0,hend = 0,bodyBegin=0,retry=0,query=0;
   while (true) {
      if (bodyBegin) {
         ssize_t r = read(buf, 1023);
         if (r<=0) {
            ffl_debug(HL,"end r: %zu", r);
            return;
         }
         buf[r] = '\0';
         ffHttp["payload"] = (ccp)buf;
         if (ffHttp["content-length"]) {
            int inL = ffHttp["content-length"];
            if (inL!=r) {
               ffHttp["cl-mismatch"]=inL;
               ffHttp["content-length"]=r;
            }
         }
         if (r>=1023) {
            ffHttp["cl-excess"]=1023;
         }
         return;
      } else {
        readagain:
         ssize_t r = read(&c, 1);
         if (r<=0) {
            ffl_debug(HL, "r: %zu", r);
            if (retry<10) {
               ++retry;
               ffl_debug(HL, "retry %d", retry);
               this_thread::sleep_for(chrono::milliseconds(200));
               ffl_debug(HL, "retry %d woke", retry);
               goto readagain;
            }
            retry=0;
            return;
         }
      }
      switch (c) {
         case '\n':
            buf[ci]='\0';
            if (!li) {
               ffl_info_contnu(HL, "version: %s\n", buf);
               ffHttp["version"]=(ccp)buf;
            } else if (hend) {
               ffHttp[(ccp)buf]=buf+hend;
               ffl_info_contnu(HL, "%s: %s\n", buf, buf+hend);
            } else if (!bodyBegin) {
               bodyBegin=1;
            }
            ++li;
            ci=spCnt=hend=0;
            continue;
         case '\r':
            continue;
         case ' ':
            if (li) {
               if (!hend || (hend==ci)) {
                  continue;
               }
               goto theDefault;
            }
            switch (spCnt) {
               case 0:
                  buf[ci]='\0';
                  if(!isValidMethod(buf)) {
                     return;
                  }
                  ffl_info_contnu(HL, "method: %s\n", buf);
                  ffHttp["method"]=(ccp)buf;
                  ci=0;
                  ++spCnt;
                  continue;
               case 1:
                  buf[ci]='\0';
                  if (!query) {
                     ffl_info_contnu(HL, "path: %s\n", buf);
                     ffHttp["path"]=(ccp)buf;
                  } else {
                     ffHttp["query"][(ccp)buf]=(ccp)buf+query;
                     query=1;
                  }
                  ci=0;
                  ++spCnt;
                  continue;
               default:
                  break;
            }
         case '?':
            if (spCnt==1) {
               buf[ci]='\0';
               ffl_info_contnu(HL, "path: %s\n", buf);
               ffHttp["path"]=(ccp)buf;
               ci=0;
               query=1;
               continue;
            }
            break;
         case '=':
            if (!li) {
               buf[ci]='\0';
               ++ci;
               query=ci;
               continue;
            }
            break;
         case '&':
            if (!li) {
               buf[ci]='\0';
               ffHttp["query"][(ccp)buf]=(ccp)buf+query;
               query=1;
               ci=0;
               continue;
            }
            break;
         case ':':
            if (li && !hend) {
               buf[ci]='\0';
               tolower((ccp)buf);
               hend=++ci;
               if (!strcmp(buf,"cookie")) {
                  parseCookie(read, ffHttp["cookie"]);
               }
               continue;
            }
      }
     theDefault:
      buf[ci]=c;
      ++ci;
      
   }
}

string mkHttpRes (const string &body,
                  const string &ctype,
                  const int code,
                  const string &codeMsg,
                  const string &addlHdrs) {
   ostringstream oss;
   oss << "HTTP/1.0 " << code << " " << codeMsg << "\r\n";
   oss << "Content-Type: " << ctype << "\r\n";
   oss << "Content-Length: " << body.size() << "\r\n";
   oss << addlHdrs;
   oss << "Connection: close\r\n\r\n";
   oss << body;
   return oss.str();
}

enum ftype {
   FSFILE, SLINK, BLINK, DIR
};
struct Entry {
   std::string name;
   ftype type;
   uintmax_t size;
   fs::file_time_type mtime;
};
string url_encode (ccp s) {
   static const char *hex = "0123456789ABCDEF";
   string out;
   int sz = strlen(s);
   out.reserve(sz *3);
   for (int i=0;i<sz; ++i) {
      unsigned char c = s[i];
      // safe characters (RFC3986 subset)
      if ( (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           c=='-' || c=='_' || c=='.' || c=='~' || c=='/' ) {
         out.push_back((char)c);
      } else {
         out.push_back('%');
         out.push_back(hex[c >> 4]);
         out.push_back(hex[c & 15]);
      }
   }
   return out;
}
string time_to_string(const fs::file_time_type &ft) {
   using namespace std::chrono;
   // portable conversion: convert from fs clock to system_clock
   auto sctp = time_point_cast<system_clock::duration>(
      ft - fs::file_time_type::clock::now()
      + system_clock::now());
   time_t tt = system_clock::to_time_t(sctp);
   tm tm{};
   localtime_r(&tt, &tm);
   char buf[64];
   strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
   return string(buf);
}

string html_escape (ccp s) {
   std::string out;
   int sz = strlen(s);
   out.reserve(sz);
   for (int i=0;i<sz;++i) {
      char c = s[i];
      switch (c) {
         case '&': out += "&amp;"; break;
         case '<': out += "&lt;";  break;
         case '>': out += "&gt;";  break;
         case '"': out += "&quot;";break;
         case '\'':out += "&#39;"; break;
         default: out += c; break;
      }
   }
   return out;
}

string get_mime_type(const fs::path &path) {
    static const unordered_map<string, string> mime {
        {".html", "text/html"},
        {".htm",  "text/html"},
        {".css",  "text/css"},
        {".js",   "text/javascript"},
        {".json", "application/json"},
        {".png",  "image/png"},
        {".jpg",  "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif",  "image/gif"},
        {".svg",  "image/svg+xml"},
        {".ico",  "image/x-icon"},
        {".txt",  "text/plain"},
        {".ttf",  "font/ttf"},
        {".pdf",  "application/pdf"},
        {".xml",  "application/xml"},
        {".zip",  "application/zip"},
        {".gz",   "application/gzip"},
        {".tar",  "application/x-tar"}
        // add more as needed
    };

    auto ext = path.extension().string();
    // lowercase the extension
    for (auto &c : ext) c = static_cast<char>(tolower(c));

    auto it = mime.find(ext);
    if (it != mime.end()) {
        return it->second;
    }
    return "application/octet-stream"; // default
}
string get_subdomain (const char* host) {
   string hoststr(host);
   if(!cfg["hostName"]) return string();
   string chost((ccp)cfg["hostName"]);
   tolower(chost);
   int domainpos =
      hoststr.find(chost.c_str());
   int portpos=hoststr.find(":");
   if (domainpos > 1)
      return hoststr.substr(0, domainpos-1);
   else
      return portpos>1?hoststr.substr(0,portpos):hoststr;
}

string myHandle (FFJSON& ffHttp) {
   FFJSON& fpath = ffHttp["path"];
   if (!fpath)
      return mkHttpRes("NaNa!");
   if (!ffHttp["host"])
      return "";
   string subdomain = get_subdomain(ffHttp["host"]);
   ffl_notice(HL, "subdomain: %s",subdomain.c_str());
   FFJSON& vhost = (bool)cfg["vhosts"][subdomain]?
      cfg["virtualWebHosts"][subdomain]:cfg;
   string path((ccp)vhost["rootdir"]);
   int plen = fpath.size;
   path+="/";
   if (plen>1)
      path+=((ccp)fpath)+1;
   else
      path+="index.html";
   fs::path fspath(path);
   if (!fs::exists(fspath))
      return ferryfair(ffHttp, vhost);
   else if (fs::is_directory(fspath)) {
      vector<Entry> entries;
      for (auto &de : fs::directory_iterator(fspath)) {
         Entry e;
         e.name = de.path().filename().string();
         e.type = de.is_directory()?ftype::DIR:de.is_symlink()?
            fs::exists(fs::status(de))?SLINK:BLINK:FSFILE;
         e.size = e.type!=FSFILE ? 0 : (de.is_regular_file() ? de.file_size():0);
         e.mtime = e.type!=BLINK?de.last_write_time():fs::file_time_type();
         entries.push_back(std::move(e));
      }
      sort(entries.begin(), entries.end(), [](auto &a, auto &b){
         return a.name < b.name;
      });
      string dirHtml = "<html><head><title>";
      dirHtml += html_escape(path.c_str())+"</title></head><body><table>";
      dirHtml += "<tr><th>Name</th><th>Size</th><th>Modified</th></tr>";
      for (auto &e : entries) {
         string disp = html_escape(
            (e.name +
             (e.type==DIR? "/":e.type==SLINK?"->":e.type==BLINK?"->x":"")
            ).c_str());
         string href = e.type!=BLINK?
            url_encode((e.name + (e.type==DIR ? "/":"")).c_str()):"";
         string sizeStr = e.type!=FSFILE? "-":to_string(e.size);
         string mtime = time_to_string(e.mtime);
         dirHtml += "<tr>";
         dirHtml += "<td><a href=\"" + href + "\">" + disp + "</a></td>";
         dirHtml += "<td>" + sizeStr + "</td>";
         dirHtml += "<td>" + mtime + "</td></tr>";
      }
      dirHtml += "</table></body></html>";
      return mkHttpRes(dirHtml,"text/html");
   } else if (path.find("/upload") || path.find("/red") || path.find("/tmp")) {
      return mkHttpRes("NaNa!");
   } else {
      ifstream reqFile(path);
      ostringstream resStr;
      resStr << reqFile.rdbuf();
      string res = resStr.str();
      return mkHttpRes(res,get_mime_type(fspath));
   }
   return mkHttpRes("NaNa!");
}

ssize_t write_all_fd (int fd, const void *buf, size_t n) {
   size_t off = 0;
   while (off < n) {
      ssize_t w = ::write(fd, (const char*)buf + off, n - off);
      if (w <= 0) return w;
      off += w;
   }
   return (ssize_t)off;
}


int ssl_write_all (SSL *ssl, const void *buf, int n) {
   int off = 0;
   while (off < n) {
      int w = SSL_write(ssl, (const char*)buf + off, n - off);
      if (w <= 0) return w;
      off += w;
   }
   return off;
}

void handle_connection (struct sockaddr_in& cli, int client_fd,
                        SSL *ssl=nullptr) {
   if (ssl && SSL_accept(ssl) <= 0) {
      ffl_err(HL, "SSL accept failed: %s",
              ERR_error_string(ERR_get_error(), nullptr));
      SSL_shutdown(ssl);
      SSL_free(ssl);
      ::close(client_fd); return;
   }
   FFJSON ffHttp;
   char ip_str[INET_ADDRSTRLEN];
   inet_ntop(AF_INET, &cli.sin_addr, ip_str, sizeof(ip_str));
   ffHttp["ip"] = (ccp)ip_str;
   
   makeNonBlocking(client_fd);
   cread nr = [client_fd] (char* buf, size_t bufSize)->size_t {
      return read(client_fd, buf, bufSize);
   };
   cread sr = [ssl] (char* buf, size_t bufSize)->size_t {
      return SSL_read(ssl, buf, bufSize);
   };
   cread r = ssl ? sr : nr;
   parseHTTP(r, ffHttp);
   string res;
   if (!ffHttp) {
      goto handledone;
   }
   ffl_info(HL, "%s %s %s %s fd=%d", (ccp)ffHttp["ip"], (ccp)ffHttp["version"],
            (ccp)ffHttp["method"], (ccp)ffHttp["path"], client_fd);
   res = myHandle(ffHttp);
   if (!res.empty()) {
      if (ssl)
         ssl_write_all(ssl, res.data(), (int)res.size());
      else
         write_all_fd(client_fd, res.data(), (int)res.size());
   }
  handledone:
   if (ssl) {
      SSL_shutdown(ssl);
      SSL_free(ssl);
   }
   ::close(client_fd);
   return;
 }

// ---------------- Networking & SSL setup ----------------
int create_listen_socket(uint16_t port) {
   int fd = ::socket(AF_INET, SOCK_STREAM, 0);
   if (fd < 0) return -1;
   int on = 1;
   setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
   struct sockaddr_in addr{};
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = INADDR_ANY;
   addr.sin_port = htons(port);
   if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      ::close(fd); return -1;
   }
   if (listen(fd, SOMAXCONN) < 0) {
      ::close(fd); return -1;
   }
   return fd;
}

SSL_CTX *create_ssl_ctx (const string &cert_file, const string &key_file) {
   SSL_library_init();
   SSL_load_error_strings();
   const SSL_METHOD *method = TLS_server_method();
   SSL_CTX *ctx = SSL_CTX_new(method);
   if (!ctx) return nullptr;
   if (
      SSL_CTX_use_certificate_file(
         ctx, cert_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
      ERR_print_errors_fp(stderr);
      SSL_CTX_free(ctx);
      return nullptr;
   }
   if (
      SSL_CTX_use_PrivateKey_file(
         ctx, key_file.c_str(), SSL_FILETYPE_PEM) <= 0
   ) {
      ERR_print_errors_fp(stderr);
      SSL_CTX_free(ctx);
      return nullptr;
   }
   if (
      !SSL_CTX_check_private_key(ctx)) {
      fprintf(stderr, "Private key does not match certificate\n"); SSL_CTX_free(ctx);
      return nullptr;
   }
   SSL_CTX_set_mode(ctx, SSL_MODE_AUTO_RETRY);
   SSL_CTX_set_options(
      ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_COMPRESSION);
   return ctx;
}
list<thread*> threadLs;
void cleanThreads () {
   list<thread*>::iterator it, dit;
   it = threadLs.begin();
   while (it != threadLs.end()) {
      dit = it;
      thread* t = *it;
      ++it;
      if (t->joinable()) {
         delete t;
         threadLs.erase(dit);
      }
   }
}

void accept_loop (int listen_fd, ThreadPool& pool, SSL_CTX* ctx = nullptr) {
   while (g_running) {
      struct sockaddr_in cli{};
      socklen_t sl = sizeof(cli);
      ffl_debug(HL, "ssl listening...");
      int c = accept(listen_fd, (struct sockaddr*)&cli, &sl);
      if (c < 0) {
         if (errno == EINTR) {
            continue;
         };
         ffl_err(HL, "accept failed: %s", strerror(errno));
         continue;
      }
      SSL* ssl = nullptr;
      if (ctx) {
         ssl = SSL_new(ctx);
         SSL_set_fd(ssl, c);
      }
      ffl_debug(HL, "got %d...", c);
      // thread t([ssl,c] () {
      //    handle_connection_ssl(ssl, c);
      // });
      // t.detach();
      pool.enqueue([&cli, ssl, c](){
         handle_connection(cli, c, ssl);
      });
   }
}

// ---------------- CLI and main ----------------
void usage_and_exit (const char *p) {
   ffl_err(HL, "Usage: %s --cert cert.pem --key key.pem [--http-port N]"
           " [--https-port N] [--docroot PATH] [--threads N]", p);
   exit(1);
}

int main (int argc, char **argv) {
   cfg.init("file://http.ffjson|OBJECT");
   ffl_debug(HL, "%s\n", cfg.prettyString().c_str());
   cfg["httpPort"]=8080;
   cfg["httpsPort"]=8443;
   cfg["threadCount"]=2*thread::hardware_concurrency();
   if (!(cfg["cert"] && cfg["key"] && cfg["ca"] &&
         cfg["docroot"])) {
      ffl_err(HL, "improper cfg");
      return 0;
   }

   signal(SIGINT, handle_sigint);
   signal(SIGPIPE, SIG_IGN);
   ffl_notice(HL, "Starting server. docroot=%s threads=%d",
              (ccp)cfg["docroot"], (int)cfg["threadCount"]);

   initFerryFair(cfg);

   int http_fd = create_listen_socket((uint16_t)(int)cfg["httpPort"]);
   if (http_fd < 0) {
      perror("http bind");
      return 1;
   }
   int https_fd = create_listen_socket((uint16_t)(int)cfg["httpsPort"]);
   if (https_fd < 0) {
      perror("https bind");
      return 1;
   }

   SSL_CTX* ssl_ctx = create_ssl_ctx(
      string((ccp)cfg["cert"]), string((ccp)cfg["key"]));
   if (!ssl_ctx) {
      ffl_err(HL, "Failed to create SSL_CTX");
      return 1;
   }

   ThreadPool pool((int)cfg["threadCount"]);

   thread t1([&] () {
      accept_loop(http_fd, pool);
   });
   thread t2([&] () {
      accept_loop(https_fd, pool, ssl_ctx);
   });
   ffl_debug(HL, "main sleeping..");
   while (g_running) {
      this_thread::sleep_for(chrono::milliseconds(2000));
   }

   ffl_notice(HL, "Shutting down...");
   ::shutdown(http_fd, SHUT_RDWR);
   ::shutdown(https_fd, SHUT_RDWR);
   ::close(http_fd);
   ::close(https_fd);
   ffl_debug(HL,"joining t1");
   if (t1.joinable()) t1.join();
   if (t2.joinable()) t2.join();
   ffl_debug(HL,"freeing ssl_ctx");
   SSL_CTX_free(ssl_ctx);
   ffl_notice(HL, "bye!");
   return 0;
}
