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

#include <FFJSON.h>
#include <logger.h>
#include <myconverters.h>
#include <FerryTimeStamp.h>

enum HTTPLOG {
   HL = 1<<11
};
FF_LOG_TYPE fflAllowedType = (FF_LOG_TYPE) (FFL_ERR | FFL_NOTICE | FFL_DEBUG |
                                            FFL_INFO);
unsigned int fflAllowedBlks = (uint)HL;

using namespace std;
namespace fs = std::filesystem;

typedef const char* ccp;

static atomic<bool> g_running{true};
FFJSON cfg;

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

map<string,string> parse_form_data(const string &body) {
   map<string,string> kv;
   istringstream ss(body);
   string pair;
   while(getline(ss, pair, '&')) {
      auto pos = pair.find('=');
      if (pos == string::npos) continue;
      string k = url_decode(pair.substr(0,pos));
      string v = url_decode(pair.substr(pos+1));
      kv[k] = v;
   }
   return kv;
}

string sanitize_path(const string &path) {
   string p = path;
   // drop query+fragment
   auto q = p.find('?'); if (q != string::npos) p = p.substr(0,q);
   auto f = p.find('#'); if (f != string::npos) p = p.substr(0,f);
   // ensure leading slash
   if (p.empty() || p[0] != '/') p = "/" + p;
   // collapse .. naive
   while(true){
      auto pos = p.find("/../");
      if (pos == string::npos) break;
      // remove previous path segment
      if (pos == 0) { p.erase(0,3); continue; }
      auto prev = p.rfind('/', pos - 1);
      if (prev == string::npos) prev = 0;
      p.erase(prev, pos - prev + 3);
   }
   return p;
}

string mime_type_from_ext(const string &path) {
   if (path.size() >= 5 && path.substr(path.size()-5) == ".html") return "text/html";
   if (path.size() >= 4 && path.substr(path.size()-4) == ".css") return "text/css";
   if (path.size() >= 3 && path.substr(path.size()-3) == ".js") return "application/javascript";
   if (path.size() >= 4 && path.substr(path.size()-4) == ".png") return "image/png";
   if (path.size() >= 4 && path.substr(path.size()-4) == ".jpg") return "image/jpeg";
   if (path.size() >= 5 && path.substr(path.size()-5) == ".jpeg") return "image/jpeg";
   if (path.size() >= 4 && path.substr(path.size()-4) == ".gif") return "image/gif";
   return "application/octet-stream";
}

// read a line (terminated by \r\n or \n) from fd (blocking)
optional<string> read_line_fd (int fd) {
   string line;
   char c;
   while (true) {
      ssize_t r = ::read(fd, &c, 1);
      if (r <= 0)
         return nullopt;
      switch (c) {
         case '\r': {
            // peek next
            ssize_t r2 = ::read(fd, &c, 1);
            if (r2 <= 0)
               return line.empty() ? nullopt : optional<string>(line);
            if (c == '\n')
               return line;
            line.push_back(c);
            break;
         }
         case '\n':
            return line;
         default:
            line.push_back(c);
      }
   }
}

// write all bytes to fd
ssize_t write_all_fd (int fd, const void *buf, size_t n) {
   size_t off = 0;
   while (off < n) {
      ssize_t w = ::write(fd, (const char*)buf + off, n - off);
      if (w <= 0) return w;
      off += w;
   }
   return (ssize_t)off;
}

// SSL helpers
optional<string> read_line_ssl (SSL *ssl) {
   string line;
   char c;
   while (true) {
      int r = SSL_read(ssl, &c, 1);
      if (r <= 0) return nullopt;
      if (c == '\r') {
         int r2 = SSL_read(ssl, &c, 1);
         if (r2 <= 0)
            return line.empty() ? nullopt : optional<string>(line);
         if (c == '\n')
            return line;
         line.push_back(c);
      } else if (c == '\n')
         return line;
      else line.push_back(c);
   }
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

// ---------------- Generic request parser (works for fd and SSL) ------------
struct HttpRequest {
   string method;
   string path;
   vector<pair<string,string>> headers;
   string body;
};

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

typedef function<size_t(char* buf, size_t bufSize)> cread;
void parseHTTP (cread read, FFJSON& ffHttp) {
   unsigned int i=0;
   unsigned int pairStartPin=i;
   char c;
   char buf[1024];
   ffl_info(HL, "request: ");
   int li=0;
   int spCnt=0;
   int ci=0,hend = 0,hstart = 0,bodyBegin=0,retry=0,query=0;
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
            } else if (hstart) {
               
               string key = string(buf,hstart);
               tolower(key);
               ffHttp[key]=buf+hstart;
               ffl_info_contnu(HL, "%s: %s\n", key.c_str(), buf+hstart);
            } else if (!bodyBegin) {
               bodyBegin=1;
            }
            ++li;
            ci=spCnt=hend=hstart=0;
            continue;
         case '\r':
            continue;
         case ' ':
            if (li) {
               if (!hstart) {
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
               hend=ci;
               continue;
            }
      }
     theDefault:
      if (li && !hstart && hend) {
         hstart=ci;
      }
      buf[ci]=c;
      ++ci;
      
   }
}

// reader function types:
//   function<optional<string>()>    line reader
//   function<int(char*,int)>        read bytes
template<typename LineReaderFn, typename ReadBytesFn>
optional<HttpRequest> parse_request_generic (
   LineReaderFn readline, ReadBytesFn readbytes
) {
   auto first = readline();
   if (!first)
      return nullopt;
   istringstream iss(*first);
   HttpRequest req;
   iss >> req.method >> req.path;
   if (req.method.empty() || req.path.empty())
      return nullopt;
   // headers
   while(true) {
      auto h = readline();
      if (!h)
         return nullopt;
      if (h->empty())
         break;
      auto pos = h->find(':');
      if (pos == string::npos)
         continue;
      string name = h->substr(0,pos);
      string value = h->substr(pos+1);
      while (!value.empty() && (value.front()==' ' || value.front()=='\t'))
         value.erase(value.begin());
      req.headers.emplace_back(name, value);
   }
   size_t content_len = 0;
   string content_type;
   for (auto &hh : req.headers) {
      if (strcasecmp(hh.first.c_str(), "Content-Length") == 0)
         content_len = (size_t)atoi(hh.second.c_str());
      if (strcasecmp(hh.first.c_str(), "Content-Type") == 0)
         content_type = hh.second;
   }
   if (content_len > 0) {
      req.body.resize(content_len);
      size_t got = 0;
      while (got < content_len) {
         int r = readbytes(&req.body[got], (int)(content_len - got));
         if (r <= 0)
            break;
         got += (size_t)r;
      }
   }
   return req;
}

// ---------------- Dynamic handlers ----------------
string make_http_response (const string &body,
                           const string &ctype = "text/plain") {
   ostringstream oss;
   oss << "HTTP/1.0 200 OK\r\n";
   oss << "Content-Type: " << ctype << "\r\n";
   oss << "Content-Length: " << body.size() << "\r\n";
   oss << "Connection: close\r\n\r\n";
   oss << body;
   return oss.str();
}

// if returns non-empty => treat as response and send directly
string handle_dynamic (HttpRequest &req) {
   // AJAX endpoint
   if (req.method == "GET" && req.path == "/ajax") {
      string json = "{\"message\":\"Hello from server via AJAX!\"}";
      return make_http_response(json, "application/json");
   }
   // form submission
   if (req.method == "POST") {
      // Check content-type (we only implement application/x-www-form-urlencoded here)
      string ctype;
      for (auto &h : req.headers) {
         if (strcasecmp(h.first.c_str(), "Content-Type") == 0) {
            ctype = h.second;
            break;
         }
      }
      // strip params
      auto semipos = ctype.find(';');
      if (semipos != string::npos) ctype = ctype.substr(0, semipos);
      // simple handling for form urlencoded
      if (ctype == "application/x-www-form-urlencoded") {
         auto kv = parse_form_data(req.body);
         ostringstream out;
         out << "Received form submission:\n";
         for (auto &p : kv) out << p.first << " = " << p.second << "\n";
         return make_http_response(out.str(), "text/plain");
      } else {
         string body = "Unsupported POST content-type or no body\n";
         return make_http_response(body, "text/plain");
      }
   }
   string res;
   return res; // empty -> no dynamic response
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
string myHandle (FFJSON& ffHttp) {
   FFJSON& fpath = ffHttp["path"];
   if (!fpath)
      return make_http_response("NaNa!");
   string path((ccp)cfg["docroot"]);
   int plen = fpath.size;
   path+="/";
   if (plen>1)
      path+=((ccp)fpath)+1;
   else
      path+="index.html";
   fs::path fspath(path);
   if (!fs::exists(fspath))
      return make_http_response("NotFound!");
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
      return make_http_response(dirHtml,"text/html");
   } else {
      ifstream reqFile(path);
      ostringstream resStr;
      resStr << reqFile.rdbuf();
      string res = resStr.str();
      return make_http_response(res,get_mime_type(fspath));
   }
   return "NaNa!";
}

// ---------------- Static file serving ----------------
bool send_404_fd (int fd) {
   string body = "404 Not Found\n";
   ostringstream oss;
   oss << "HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: " << body.size() << "\r\nConnection: close\r\n\r\n" << body;
   return write_all_fd(fd, oss.str().data(), oss.str().size()) > 0;
}
bool send_404_ssl (SSL *ssl) {
   string body = "404 Not Found\n";
   ostringstream oss;
   oss << "HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\nContent-Length: " << body.size() << "\r\nConnection: close\r\n\r\n" << body;
   return ssl_write_all(ssl, oss.str().data(), (int)oss.str().size()) > 0;
}

void serve_static_fd (int fd, const string &path) {
   string sp = sanitize_path(path);
   string full = string((ccp)cfg["doc_root"]) + sp;
   if (!full.empty() && full.back() == '/') full += "index.html";
   struct stat st;
   if (stat(full.c_str(), &st) == -1 || !S_ISREG(st.st_mode)) { send_404_fd(fd); return; }
   ifstream ifs(full, ios::binary);
   ostringstream bodyss;
   bodyss << ifs.rdbuf();
   string body = bodyss.str();
   ostringstream hdr;
   hdr << "HTTP/1.0 200 OK\r\n";
   hdr << "Content-Type: " << mime_type_from_ext(full) << "\r\n";
   hdr << "Content-Length: " << body.size() << "\r\n";
   hdr << "Connection: close\r\n\r\n";
   auto h = hdr.str();
   write_all_fd(fd, h.data(), h.size());
   write_all_fd(fd, body.data(), body.size());
}

void serve_static_ssl (SSL *ssl, int fd, const string &path) {
   string sp = sanitize_path(path);
   string full = string((ccp)cfg["doc_root"]) + sp;
   if (!full.empty() && full.back() == '/') full += "index.html";
   struct stat st;
   if (stat(full.c_str(), &st) == -1 || !S_ISREG(st.st_mode)) { send_404_ssl(ssl); return; }
   ifstream ifs(full, ios::binary);
   ostringstream bodyss;
   bodyss << ifs.rdbuf();
   string body = bodyss.str();
   ostringstream hdr;
   hdr << "HTTP/1.0 200 OK\r\n";
   hdr << "Content-Type: " << mime_type_from_ext(full) << "\r\n";
   hdr << "Content-Length: " << body.size() << "\r\n";
   hdr << "Connection: close\r\n\r\n";
   auto h = hdr.str();
   ssl_write_all(ssl, h.data(), (int)h.size());
   ssl_write_all(ssl, body.data(), (int)body.size());
}
// ---------------- Connection handlers ----------------
void handle_connection_plain (int client_fd) {
   FFJSON ffHttp;
   makeNonBlocking(client_fd);
   auto r = [client_fd] (char* buf, size_t bufSize)->size_t {
      return read(client_fd, buf, bufSize);
   };
   parseHTTP(r, ffHttp);
   //auto req_opt = parse_request_generic(line_reader, readbytes);
   if (!ffHttp) {
      ::close(client_fd);
      return;
   }
   ffl_info(HL, "HTTP %s %s fd=%d", (ccp)ffHttp["method"], (ccp)ffHttp["path"],
            client_fd);
   // dynamic?
   string dyn = myHandle(ffHttp);
   if (!dyn.empty()) {
      write_all_fd(client_fd, dyn.data(), dyn.size());
      ::close(client_fd);
      return;
   }
   ::close(client_fd);
}

void handle_connection_ssl (SSL *ssl, int client_fd) {
   if (!ssl) { ::close(client_fd); return; }
   if (SSL_accept(ssl) <= 0) {
      ffl_err(HL, "SSL accept failed: %s",
              ERR_error_string(ERR_get_error(), nullptr));
      SSL_shutdown(ssl);
      SSL_free(ssl);
      ::close(client_fd); return;
   }
   FFJSON ffHttp;
   makeNonBlocking(client_fd);
   auto r = [ssl] (char* buf, size_t bufSize)->size_t {
      return SSL_read(ssl, buf, bufSize);
   };
   parseHTTP(r, ffHttp);
   if (!ffHttp) {
      ::close(client_fd);
      return;
   }
   ffl_info(HL, "HTTP %s %s fd=%d", (ccp)ffHttp["method"], (ccp)ffHttp["path"],
            client_fd);
   string dyn = myHandle(ffHttp);
   if (!dyn.empty()) {
      ssl_write_all(ssl, dyn.data(), (int)dyn.size());
      SSL_shutdown(ssl);
      SSL_free(ssl);
      ::close(client_fd);
      return;
   }
   SSL_shutdown(ssl); SSL_free(ssl); ::close(client_fd);
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
// accept loops
void accept_loop_plain (int listen_fd) {
   while (g_running) {
      struct sockaddr_in cli{};
      socklen_t sl = sizeof(cli);
      ffl_debug(HL, "plain listening...");
      int c = accept(listen_fd, (struct sockaddr*)&cli, &sl);
      if (c < 0) {
         if (errno == EINTR) {
            ffl_debug(HL, "accept_loop_plain interrupted");
            continue;
         }
         ffl_err(HL, "accept failed: %s", strerror(errno));
         continue;
      }
      ffl_debug(HL, "got %d...", c);
      thread t([c] () {
         handle_connection_plain(c);
      });
      t.detach();
   }
}

void accept_loop_ssl (int listen_fd, SSL_CTX *ctx) {
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
      SSL *ssl = SSL_new(ctx);
      SSL_set_fd(ssl, c);
      ffl_debug(HL, "got %d...", c);
      thread t([ssl,c] () {
         handle_connection_ssl(ssl, c);
      });
      t.detach();
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

   thread t1([&] () {
      accept_loop_plain(http_fd);
   });
   thread t2([&] () {
     accept_loop_ssl(https_fd, ssl_ctx);
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
