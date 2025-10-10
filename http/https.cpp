// author: gowtham
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
#include <zlib.h>

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
#include <sys/epoll.h>
#include <errno.h>
#include <sys/socket.h>

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
   ffl_notice(HL, "Shutting down...");
}

string gzipCompress (
   const string &data, int level = Z_BEST_COMPRESSION) {
   string compressed;
   
   z_stream zs{};
   zs.zalloc = Z_NULL;
   zs.zfree = Z_NULL;
   zs.opaque = Z_NULL;
   
   // 16 + MAX_WBITS tells zlib to write a gzip header/trailer
   if (deflateInit2(&zs, level, Z_DEFLATED, 16 + MAX_WBITS, 8,
                    Z_DEFAULT_STRATEGY) != Z_OK) {
      return compressed;
   }
   
   zs.next_in = reinterpret_cast<Bytef*>(const_cast<char *>(data.data()));
   zs.avail_in = data.size();
   
   const size_t CHUNK_SIZE = 16384;
   std::vector<unsigned char> outbuffer(CHUNK_SIZE);
   
   int ret;
   do {
      zs.next_out = outbuffer.data();
      zs.avail_out = outbuffer.size();
      
      ret = deflate(&zs, zs.avail_in ? Z_NO_FLUSH : Z_FINISH);
      if (compressed.size() < zs.total_out) {
         compressed.insert(compressed.end(),
                           outbuffer.data(),
                           outbuffer.data() +
                           (zs.total_out - compressed.size()));
      }
   } while (ret == Z_OK);
   
   deflateEnd(&zs);
   
   if (ret != Z_STREAM_END) {
      compressed.clear();
   }
   
   return compressed;
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
string url_decode (const string &s) {
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

using crdwr = function<size_t(char*, size_t)>;
void parseHost (crdwr read, FFJSON& host) {
   char c;
   string buf;
   FFJSON& dom = host["domain"];
   FFJSON& subs = dom["subs"];
   subs.init("[]");
   bool port = false;
   int ci=0;
   while (read(&c, 1)>0) {
      switch (c) {
         case '\r':
            continue;
         case '\n':
            ffl_info_contnu(HL,"%s\n", buf.c_str());
            dom["name"]=buf;
            return;
         case ' ':
            if (!buf.length()) {
               continue;
            }
         case ':':
         case '.':
            subs[subs.size]=ci;
         default:
            buf+=c;
            ++ci;
            break;
      }
   }
}
void parseCookie (crdwr read, FFJSON& ffCookie) {
   char c;
   string key,value;
   string* buf = &key;
   while (read(&c, 1)>0) {
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
            ffl_info_contnu(HL,"%s=%s",key.c_str(),value.c_str());
            if (c=='\n') {
               ffl_info_contnu(HL,"\n");
               return;
            } else {
               ffl_info_contnu(HL,";");
               buf=&key;
            }
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

void parseAcceptEncoding (crdwr read, FFJSON& ffAEnc) {
   char c;
   string enc;
   while (read(&c, 1)>0) {
      switch (c) {
         case ',':
            ffAEnc[]=enc;
            ffl_info_contnu(HL, "%s,", enc.c_str());
            enc.clear();
         case ' ':
         case '\r':
            continue;
         case '\n':
            ffAEnc[]=enc;
            ffl_info_contnu(HL, "%s\n", enc.c_str());
            return;
         default:
            enc+=c;
            break;
      }
   }
   return;
}

void parseHTTP (crdwr read, FFJSON& ffHttp) {
   unsigned int i=0;
   unsigned int pairStartPin=i;
   char c;
   char buf[1024];
   ffl_info(HL, "request: ");
   int li=0;
   int spCnt=0;
   int ci=0,hend = 0,bodyBegin=0,query=0;
   while (true) {
      if (bodyBegin) {
         if (ffHttp["content-length"]) {
            int inL = atoi((ccp)ffHttp["content-length"]);
            ffHttp["content-length"]=inL;
            uint8_t* pbuf = new uint8_t[inL+1];
            ssize_t r = read((char*)pbuf, inL);
            if (r<=0) {
               ffl_debug(HL,"end r: %zd", r);
               delete[] pbuf;
               return;
            }
            if (inL!=r) {
               ffl_err(HL, "payload != content-length");
               delete[] pbuf;
               return;
            }
            pbuf[r] = '\0';
            FFJSON::Blob_ b;
            b.p = pbuf;
            b.s = inL+1;
            ffHttp["payload"] = b;
         }
         return;
      } else {
         ssize_t r = read(&c, 1);
         if (r<=0) {
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
               ffHttp[(ccp)buf]=(ccp)(buf+hend);
               ffl_info_contnu(HL, "%s\n", (ccp)(buf+hend));
            } else if (!bodyBegin) {
               if (!ffHttp["content-length"])
                  return;
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
               ffl_info_contnu(HL,"%s: ",buf);
               tolower((ccp)buf);
               if (!strcmp(buf,"cookie")) {
                  parseCookie(read, ffHttp["cookie"]);
               } else if (!strcmp(buf,"host")) {
                  parseHost(read, ffHttp["host"]);
               } else if (!strcmp(buf,"accept-encoding")) {
                  parseAcceptEncoding(read, ffHttp["accept-encoding"]);
               } else {
                  hend=++ci;
                  continue;
               }
               ci=0;
               continue;
            }
      }
     theDefault:
      buf[ci]=c;
      ++ci;
      
   }
}

string mkHttpRes (FFJSON& ffHttp, const string& body,
                  const string &ctype,
                  const int code,
                  const string &codeMsg,
                  const string &addlHdrs) {
   ostringstream oss;
   oss << "HTTP/1.0 " << code << " " << codeMsg << "\r\n";
   oss << "Content-Type: " << ctype << "\r\n";
   oss << addlHdrs;
   oss << "Connection: close\r\n";
   int ocl = body.size();
   FFJSON& accEnc = ffHttp["accept-encoding"];
   if (ocl>1024 && accEnc && accEnc["gzip"] &&
       ctype.find("image")==string::npos) {
      string gz = gzipCompress(body);
      oss << "Content-Encoding: gzip\r\n";
      oss << "Content-Length: " << gz.size() << "\r\n\r\n";
      oss << gz;
   } else {
      oss << "Content-Length: " << ocl << "\r\n\r\n";
      oss << body;
   }
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

string get_mime_type(const fs::path& path) {
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

string httpHandle (FFJSON& ffHttp) {
   FFJSON& fpath = ffHttp["path"];
   if (!fpath)
      return mkHttpRes(ffHttp, "NaNa!");
   if (!ffHttp["host"])
      return "";
   FFJSON& domain = ffHttp["host"]["domain"];
   ccp domname = domain["name"];
   string subdomain(domname,(int)domain["subs"][0]);
   FFJSON& vhost = cfg["vhosts"][subdomain]?cfg["vhosts"][subdomain]:cfg;
   string path((ccp)vhost["rootdir"]);
   int plen = fpath.size;
   string res;
   path+="/";
   if (plen>1)
      path+=((ccp)fpath)+1;
   else
      path+="index.html";
   ffl_info(HL,"serving %s", path.c_str());
   fs::path fspath(path);
   res = ferryfair(ffHttp, vhost);
   if (res.length()) {
      return res;
   }
   if (fs::is_directory(fspath)) {
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
      return mkHttpRes(ffHttp, dirHtml,"text/html");
   } else if (path.find("/red")!=string::npos ||
              path.find("/tmp")!=string::npos) {
      return mkHttpRes(ffHttp, "NaNa!");
   } else {
      ifstream reqFile(path);
      ostringstream resStr;
      resStr << reqFile.rdbuf();
      string res = resStr.str();
      return mkHttpRes(ffHttp, res,get_mime_type(fspath));
   }
   return mkHttpRes(ffHttp, "NaNa!");
}

void handle_connection (struct sockaddr_in cli, int client_fd,
                        SSL* ssl = nullptr) {
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
   
   //makeNonBlocking(client_fd);
   crdwr nr = [client_fd] (char* buf, size_t bufSize)->size_t {
      return recv(client_fd, buf, bufSize, 0);
   };
   crdwr sr = [ssl] (char* buf, size_t bufSize)->size_t {
      return SSL_read(ssl, buf, bufSize);
   };
   crdwr rd = ssl ? sr : nr;
   crdwr rr = [&rd, client_fd] (char* buf, size_t bufSize)->size_t {
      int retry = cfg["readRetry"];
      static int retryMS = cfg["retryMS"];
     readagain:
      ssize_t r = rd(buf, bufSize);
      if (r<0 && retry>0) {
         ffl_debug(HL, "r: %zd", r);
         if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            --retry;
            ffl_debug(HL, "retry %d", retry);
            this_thread::sleep_for(chrono::milliseconds(retryMS));
            ffl_debug(HL, "retry %d woke", retry);
            goto readagain;
         }
      }
      return r;
   };
   parseHTTP(rr, ffHttp);
   string res;
   if (!ffHttp["version"]) {
      goto handledone;
   }
   ffl_info(HL, "%s %s %s %s fd=%d", (ccp)ffHttp["ip"], (ccp)ffHttp["version"],
            (ccp)ffHttp["method"], (ccp)ffHttp["path"], client_fd);
   res = httpHandle(ffHttp);
   if (!res.empty()) {
      crdwr nw = [client_fd] (char* buf, size_t bufSize)->size_t {
         return write(client_fd, buf, bufSize);
      };
      crdwr sw = [ssl] (char* buf, size_t bufSize)->size_t {
         return SSL_write(ssl, buf, bufSize);
      };
      crdwr wd = ssl ? sw : nw;
      crdwr rw = [&wd, client_fd] (char* buf, size_t bufSize)->size_t {
         int retry = cfg["readRetry"];
         static int retryMS = cfg["retryMS"];
         size_t off = 0;
         while (off < bufSize) {
           writeagain:
            ssize_t w = wd(buf+off, bufSize - off);
            if (w<=0) {
               ffl_notice(
                  HL, "fd: %d, write socket error: %d(%s)@%zd/%zd", client_fd,
                  errno, strerror(errno), off , bufSize);
               if (retry>0 && (errno==EAGAIN || errno==EINTR)) {
                  ffl_debug(HL, "fd: %d, w: %zd", client_fd, w);
                  --retry;
                  this_thread::sleep_for(chrono::milliseconds(retryMS));
                  ffl_debug(HL, "fd: %d, write retry %d woke",
                            client_fd, retry);
                  goto writeagain;
               } else {
                  ffl_notice(HL,"fd: %d, write error, closing at %d",
                             client_fd, off);
                  return off;
               }
               off += w;
            }
         }
         return off;
      };
      rw(res.data(),res.size());
   }
   
  handledone:
   if (ssl) {
      SSL_shutdown(ssl);
      SSL_free(ssl);
   }
   ::close(client_fd);
   ffl_debug(HL, "%d fd closed", client_fd);
   return;
 }

// ---------------- Networking & SSL setup ----------------
int create_listen_socket (uint16_t port) {
   int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
   if (fd < 0) return -1;
   int on = 1;
   setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
   struct sockaddr_in addr{};
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = INADDR_ANY;
   addr.sin_port = htons(port);
   ffl_info(HL, "listening on %d", port);
   if (::bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
      ::close(fd); return -1;
   }
   if (::listen(fd, SOMAXCONN) < 0) {
      ::close(fd); return -1;
   }
   return fd;
}

SSL_CTX* create_ssl_ctx (const string &cert_file, const string &key_file) {
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

void accept_loop (int listen_fd, ThreadPool& pool, SSL_CTX* ctx = nullptr) {
   while (g_running) {
      struct sockaddr_in cli{};
      socklen_t sl = sizeof(cli);
      ffl_debug(HL, "listening on %d...", listen_fd);
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
      pool.enqueue([cli, ssl, c](){
         handle_connection(cli, c, ssl);
      });
      if (saveFerryfair) {
         thread(saveFerryFair,(void*)&cfg).detach();
      }
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
   ffl_debug(HL, "EAGAIN(%zd) EINTR(%zd) EINVAL(%zd)\n",
             EAGAIN, EINTR, EINVAL);
   cfg["threadCount"]=2*thread::hardware_concurrency();
   if (!(cfg["cert"] && cfg["key"] && cfg["ca"])) {
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
