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


int ssl_write_all (SSL *ssl, const void *buf, int n) {
   int off = 0;
   while (off < n) {
      int w = SSL_write(ssl, (const char*)buf + off, n - off);
      if (w <= 0) return w;
      off += w;
   }
   return off;
}

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
// template<typename LineReaderFn, typename ReadBytesFn>
// optional<HttpRequest> parse_request_generic (
//    LineReaderFn readline, ReadBytesFn readbytes
// ) {
//    auto first = readline();
//    if (!first)
//       return nullopt;
//    istringstream iss(*first);
//    HttpRequest req;
//    iss >> req.method >> req.path;
//    if (req.method.empty() || req.path.empty())
//       return nullopt;
//    // headers
//    while(true) {
//       auto h = readline();
//       if (!h)
//          return nullopt;
//       if (h->empty())
//          break;
//       auto pos = h->find(':');
//       if (pos == string::npos)
//          continue;
//       string name = h->substr(0,pos);
//       string value = h->substr(pos+1);
//       while (!value.empty() && (value.front()==' ' || value.front()=='\t'))
//          value.erase(value.begin());
//       req.headers.emplace_back(name, value);
//    }
//    size_t content_len = 0;
//    string content_type;
//    for (auto &hh : req.headers) {
//       if (strcasecmp(hh.first.c_str(), "Content-Length") == 0)
//          content_len = (size_t)atoi(hh.second.c_str());
//       if (strcasecmp(hh.first.c_str(), "Content-Type") == 0)
//          content_type = hh.second;
//    }
//    if (content_len > 0) {
//       req.body.resize(content_len);
//       size_t got = 0;
//       while (got < content_len) {
//          int r = readbytes(&req.body[got], (int)(content_len - got));
//          if (r <= 0)
//             break;
//          got += (size_t)r;
//       }
//    }
//    return req;
// }

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
// string handle_dynamic (HttpRequest &req) {
//    // AJAX endpoint
//    if (req.method == "GET" && req.path == "/ajax") {
//       string json = "{\"message\":\"Hello from server via AJAX!\"}";
//       return make_http_response(json, "application/json");
//    }
//    // form submission
//    if (req.method == "POST") {
//       // Check content-type (we only implement application/x-www-form-urlencoded here)
//       string ctype;
//       for (auto &h : req.headers) {
//          if (strcasecmp(h.first.c_str(), "Content-Type") == 0) {
//             ctype = h.second;
//             break;
//          }
//       }
//       // strip params
//       auto semipos = ctype.find(';');
//       if (semipos != string::npos) ctype = ctype.substr(0, semipos);
//       // simple handling for form urlencoded
//       if (ctype == "application/x-www-form-urlencoded") {
//          auto kv = parse_form_data(req.body);
//          ostringstream out;
//          out << "Received form submission:\n";
//          for (auto &p : kv) out << p.first << " = " << p.second << "\n";
//          return make_http_response(out.str(), "text/plain");
//       } else {
//          string body = "Unsupported POST content-type or no body\n";
//          return make_http_response(body, "text/plain");
//       }
//    }
//    string res;
//    return res; // empty -> no dynamic response
// }
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
   if(!config["hostName"]) return string();
   string chost((ccp)config["hostName"]);
   tolower(chost);
   int domainpos =
      hoststr.find(chost.c_str());
   int portpos=hoststr.find(":");
   if (domainpos > 1)
      return hoststr.substr(0, domainpos-1);
   else
      return portpos>1?hoststr.substr(0,portpos):hoststr;
}

void get_cookies (const char* c, FFJSON& fc) {
   unsigned i = 0;
   unsigned pairStartPin=i;
   string first,second;
   while (c[i]!='\0') {
      if(c[i]=='='){
         while(c[pairStartPin]==' ')++pairStartPin;
         first=string(c+pairStartPin,i-pairStartPin);
         pairStartPin=i+1;
      } else if (c[i+1]==';' || c[i+1]=='\0') {
         second=string(c+pairStartPin, i+1-pairStartPin);
         pairStartPin=i+2;
         fc[first]=second;
      }
      ++i;
   }
}

string ferryfair (FFJSON& ffHttp) {
   FFJSON cookie, payload, reply, user, rbsid;
   FFJSON urlData;
   string subdomain;
   ccp referer=nullptr;char proto[8]="https"; int protolen;
   ccp username = nullptr, password = nullptr, cpld = nullptr;
   ccp jsonHeader = "content-type: text/json\r\n";
   ccp headers = jsonHeader, path;
   string bid;
   FFJSON& rbs=vhost["rbs"];
   FFJSON& users=vhost["users"];
   if (vhost["rootdir"])
      opts.root_dir=vhost["rootdir"];
   if (sessionData["cookie"])get_cookies(sessionData["cookie"], cookie);
   ffl_notice(FPL_HTTPSERV, "cookie[bid]: %s",(ccp)cookie["bid"]);
   if (cookie["bid"]) {
      bid = (ccp)cookie["bid"];
   }
   auto now = chrono::system_clock::now();
   auto now_ms =
      chrono::time_point_cast<chrono::milliseconds>(now);
   long lepoch = now_ms.time_since_epoch().count();
   if (vhost["redirect"]) {
      char rhed[64];
      sprintf(rhed, "Location: %s\r\n", (ccp)vhost["redirect"]);
      mg_http_reply(c, 308, rhed, "Permanent Redirect",
                    (ccp)vhost["redirect"]);
      goto done;
   }
   if (!sessionData["referer"]) goto nextproto;
   referer = sessionData["referer"];
   username = strstr(referer,":");
   protolen = username - referer;
   if (username==nullptr || protolen<0 || protolen>=8) {
      ffl_debug(FPL_HTTPSERV, "badproto");
      mg_http_reply(c, 200, headers, "badproto");
      goto done;
   }
   sprintf(proto,"%.*s",protolen,(ccp)sessionData["referer"]);
  nextproto:
   username=nullptr;
   ffl_debug(FPL_HTTPSERV, "proto: %s",proto);
   ffl_notice(FPL_HTTPSERV, "Serving: %s", opts.root_dir);
   path = sessionData["path"];
   const char* pathStart;
   pathStart = strstr(path,"/sleep?");
   if (pathStart) {
      struct thread_data *data =
         (struct thread_data *) calloc(1, sizeof(*data));  // Worker owns it
      data->data = (void*)(size_t)atoi(pathStart+7); // Pass message
      data->conn_id = c->id;
      data->mgr = c->mgr;
      start_thread(thread_function, data);  // Start thread and pass data
      goto done;
   }
   if (strstr(path, "/activate?")) {
      get_data_in_url(path, urlData);
      username=urlData["user"];
      user=&users[username];
      if ((!user["password"] || !user["inactive"]) &&
          !user["newpassword"]) {
         mg_http_reply(c, 400, headers, "{%Q:%Q}", "error", "wrongKey" );
      } else if (!strcmp(user["activationKey"],urlData["key"])) {
         if (user["newpassword"]) {
            user["password"]=user["newpassword"];
            user["newpassword"]=false;
         }
         user["name"]=username;
         user["inactive"]=false;
         user["things"].init("[]");
         user["smsgs"].init("[]");
         user["reps"].init("[]");
         mg_http_reply(c, 200, headers, "%s activated.", username);
         users.save();
      } else {
         mg_http_reply(c, 400, headers, "{%Q:%Q}", "error", "wrongKey" );
      }
      goto done;
   }
         
   cpld = (ccp)sessionData["payload"];
   if (!cpld) {
      goto bidcheck2;
   }
      
   if (strstr(path, "/cookie")==path) {
      //cookie
      ffl_notice(FPL_HTTPSERV, "cookie");
      if (bid.length())
         if(rbs[bid])
            goto gotbid;
     newbid:
      bid = random_alphnuma_string();
     bidcheck:
      if (rbs[bid]) {
         bid=random_alphnuma_string();
         goto bidcheck;
      }
      rbs[bid]["ip"]=*(uint32_t*)(c->rem.ip);
     gotbid:
      if ((uint32_t)rbs[bid]["ip"]!=*(uint32_t*)(c->rem.ip)) {
         goto newbid;
      }
      rbsid = &rbs[bid];
      rbsid["ts"]=now;
      reply["bid"]=bid;
      get_data_in_url(path, urlData);
      set<FFJSON*>& mdts = bidThings[&rbsid];
      Pts pts;
      if (urlData["user"] && urlData["thing"]) {
         FFJSON& uthings = users[(ccp)urlData["user"]]["things"];
         int tind = getIdChildInd(uthings, atoi(urlData["thing"]));
         FFJSON* thn = &uthings[tind];
         reply["things"][0]=thn;
         mdts.clear();
         mdts.insert(thn);
         FFJSON q("{things:!}");
         user.answerObject(&q, nullptr, FerryTimeStamp(), &reply);
         goto cookieReply;
      }
      payload.init(cpld);
      if (!payload["geoposition"].isType(FFJSON::UNDEFINED) &&
          payload["geoposition"].size==2
      ) {
         pts.c.x=(float)payload["geoposition"][1];
         pts.c.y=(float)payload["geoposition"][0];
         rbsid["geoposition"] = payload["geoposition"];
      }
      thnsTree.getPointsFromQuad(pts);
      mdts.clear();
      for (uint i = 0; i<pts.pts.size(); ++i) {
         NdNPrn& nd = pts.pts[i];
         FFJSON* f;
         if (nd.prn==(QuadNode*)-1) {
            f = (FFJSON*)nd.qh;
         } else {
            auto aa = getNode(nd);
            f = (FFJSON*)get<0>(aa);
         }
         reply["things"][i]=f;
         mdts.insert(f);
      }
      username = rbsid["user"];
      if (username && (user = &users[username]) &&
          !strcmp((ccp)user["bid"],bid.c_str())) {
         rbsid["urts"]=lepoch;
         addSmtgsToReply(users, user, reply, mdts);
      }
     cookieReply:
      mg_http_reply(c, 200, headers, "%s",
                    reply.stringify(true).c_str());
      rbs.save();
      goto done;
   }
  bidcheck2:
   if (!bid.length() || !rbs[bid]) {
      goto fileserver;
   }
   rbsid = &rbs[bid];
   if (!cpld) {
      if (strstr(path, "/upload?chunkSize=")) {
         goto upload;
      }
      goto allfileserver;
   }
   if (!strcmp(path, "/captcha")) {
      ffl_notice(FPL_HTTPSERV, "captcha");
      string tempPath(string(opts.root_dir)+"/tmp/"+bid+".jpg");
      string randstr = random_alphnuma_string(7);
      cap randcap(randstr, tempPath, 7, 288, 68, 40, 80, 48);
      rbsid["captcha"]=randstr;
      randcap.save();
      mg_http_reply(c, 200, headers, "{%Q:%s}", "cap","true");
      rbs.save();
      goto done;
   } else if (!strcmp(path, "/login")) {
      ffl_notice(FPL_HTTPSERV, "Login");
      payload.init(cpld);
      username=payload["username"];password=payload["password"];
      ffl_notice(FPL_HTTPSERV, "\nUser: %s\nPass: %s", username, password);
      if (!users[username]) {
         mg_http_reply(c, 200, headers, "{%Q:%s}", "login","false");
         goto done;
      }
      user=&users[username];
      cout << "password:" << (ccp)user["password"] << endl;
      if (user["password"] && !user["inactive"] &&
          !strcmp(password,user["password"])
      ) {
         rbsid["user"]=user["name"];
         rbsid["ip"]=*(uint32_t*)(c->rem.ip);
         user["bid"]=bid;
         rbsid["urts"]=lepoch;
         addSmtgsToReply(users, user, reply, bidThings[&rbsid]);
         mg_http_reply(c, 200, headers, "%s",
                       reply.stringify(true).c_str());
         rbs.save();
         users.save();
      } else {
         mg_http_reply(c, 200, headers, "{%Q:%s}", "login","false");
      }
      goto done;
   } else if (!strcmp(path, "/signup")) {
      //signup
      payload.init(cpld);
      bool recovery=false;
      ffl_notice(FPL_HTTPSERV, "Signup");
      if (!isValidEmail(payload["email"])) {
         ffl_warn(FPL_HTTPSERV, "invalid email.");
         mg_http_reply(c, 400, headers, "{%Q:%Q}", "error","yay");
         goto done;

      }
      if (payload["username"]) {
         username=payload["username"];
         ffl_debug(FPL_HTTPSERV, "User: %s\nPass: %s\nEmail: %s",
                   username, password, (ccp)payload["email"]);
      } else if (payload["email"]) {
         recovery=true;
         std::map<string,FFJSON*>* emln = users.val.pairs;
         if (emln->find(string((ccp)payload["email"]))!=emln->end()) {
            FFJSON* ffemln = (*emln)[string((ccp)payload["email"])];
            FFJSON::Link* link =
               ffemln->getFeaturedMember(FFJSON::FM_LINK).link;
            username=(*link)[0].c_str();
            ffl_debug(FPL_HTTPSERV, "username: %s", username);
         } else {
            ffl_warn(FPL_HTTPSERV, "%s Email not registered.",
                     (ccp)payload["email"]);
            mg_http_reply(c, 200, headers, "{%Q:%d,%Q:%Q}", "actEmailSent",
                          -5, "msg",
                          "Email not registered!");
            goto done;
         }
      }
      password=payload["password"];
      user=&users[username];
      if (!recovery && user &&
          (user["activationKey"] && !user["inactive"])) {
         ffl_warn(FPL_HTTPSERV, "User already exists.");
         mg_http_reply(c, 200, headers, "{%Q:%d,%Q:%Q}", "actEmailSent",
                       -1, "msg",
                       "Username already taken, choose an another :|");
         goto done;
      } else if (!recovery && user["inactive"] &&
                 strcmp(payload["email"],user["email"])) {
         ffl_warn(FPL_HTTPSERV, "User exists; mail mismatch");
         mg_http_reply(c, 200, headers, "{%Q:%d,%Q:%Q}", "actEmailSent",
                       -2, "msg",
                       "Username already taken, choose an another :|");
         goto done;
      } else if (
         !recovery && users[(ccp)payload["email"]] && !user["email"]
      ) {
         ffl_warn(FPL_HTTPSERV, "Email already registered.");
         mg_http_reply(c, 200, headers, "{%Q:%d,%Q:%Q}", "actEmailSent",
                       -3, "msg",
                       "Email already registered! Try resetting password");
         goto done;
      } else if (!recovery && !(validUsername(string(username)))) {
         ffl_warn(FPL_HTTPSERV, "Invalid password");
         mg_http_reply(c, 200, headers, "{%Q:%d,%Q:%Q}", "actEmailSent",
                       -6, "msg", "Invalid password X|");
         goto done;
      } else if (
         !(password!=nullptr && validMD5(string(password)))
      ) {
         ffl_warn(FPL_HTTPSERV, "Invalid password");
         mg_http_reply(c, 200, headers, "{%Q:%d,%Q:%Q}", "actEmailSent",
                       -6, "msg", "Invalid password X|");
         goto done;
      } else if (
         !payload["captcha"] || !rbsid["captcha"] ||
         strcmp((ccp)payload["captcha"],(ccp)rbsid["captcha"])!=0
      ) {
         ffl_warn(FPL_HTTPSERV, "Captcha mismatch.");
         mg_http_reply(c, 200, headers, "{%Q:%d,%Q:%Q}", "actEmailSent",
                       -4, "msg", "Captcha mismatch, hmm!");
         goto done;
      } else if (!payload["consent"]) {
         ffl_warn(FPL_HTTPSERV, "Captcha mismatch.");
         mg_http_reply(c, 200, headers, "{%Q:%d,%Q:%Q}", "actEmailSent",
                       -5, "msg",
                       "U didn't consent to this tool usage :/");
         goto done;
      }
      if (!recovery) {
         user["email"] = payload["email"];
         user["password"] = password;
         users[(ccp)user["email"]].addLink(users,username);
         user["inactive"]=true;
         filesystem::path
            usrpth(string(opts.root_dir)+string("/upload/")+username);
         filesystem::create_directory(usrpth);
      } else {
         user["newpassword"] = password;
      }
      string actKey = random_alphnuma_string();
      user["activationKey"]=actKey;
      ffl_notice(FPL_HTTPSERV, "actKey: %s",actKey.c_str());
      to=user["email"];
      sprintf(subj, "User activation link");
      sprintf(mesg, "Open %s://%s/activate?user=%s&key=%s to activate "
              "%s", proto, (ccp)sessionData["host"], username,
              (ccp)user["activationKey"], username);
      mail_server = vhost["config"]["secret"]["mail_server"];
      admin = vhost["config"]["secret"]["admin"];
      admin_pass = vhost["config"]["secret"]["admin_pass"];
      mg_connect(&mail_mgr, mail_server, mailfn, NULL);
      while(!s_quit)
         mg_mgr_poll(&mail_mgr, 100);
      s_quit=false;
      mg_http_reply(c, 200, headers, "{%Q:%d,%Q:%Q}", "actEmailSent", 2,
                    "msg", "Activation mail sent to ur email :D");
      rbs.save();
      users.save();
      goto done;
   } else if (strstr(path, "/search")) {
      payload.init(cpld);
      ccp srchStr = payload["search"];
      Pts pts;
      vector<string> mstr = metaname(srchStr);
      pts.ina = nametouint(mstr);
      int k=0;
      if (!payload["geoposition"].isType(FFJSON::UNDEFINED) &&
          payload["geoposition"].size==2
      ) {
         pts.c.x=(float)payload["geoposition"][1];
         pts.c.y=(float)payload["geoposition"][0];
         rbsid["geoposition"] = payload["geoposition"];
      }
      ffl_info(FPL_HTTPSERV, "searching %s at %s\n",srchStr,
               payload["geoposition"].stringify().c_str());
      CompThingNameMatch cTNM;
      multiset<tuple<FFJSON*, int8_t>, CompThingNameMatch> score(cTNM);
      thnsTree.getPointsFromQuad(pts);
      for (int i=0;i<pts.pts.size();++i) {
         NdNPrn& nd = pts.pts[i];
         FFJSON* f;
         if (nd.prn==(QuadNode*)-1) {
            f = (FFJSON*)nd.qh;
         } else {
            auto aa = getNode(nd);
            f = (FFJSON*)get<0>(aa);
         }
         score.insert({f,nd.d.x});
      }
      multiset<tuple<FFJSON*, int8_t>, CompThingNameMatch>::iterator it=
         score.begin();
      rbsid = &rbs[bid];
      set<FFJSON*>& mdts = bidThings[&rbsid];
      while (it!=score.end()) {
         FFJSON& f = *get<0>(*it);
         bool thingIsWithUser = mdts.find(&f)!=mdts.end();
         if (thingIsWithUser) {
            FFJSON& rt = reply["things"][k];
            rt["id"]=f["id"];
            rt["user"]=&f["user"]["name"];
         } else {
            reply["things"][k]=&f;
         }
         ++k;++it;
      }
      reply["things"][0];
      mg_http_reply(c, 200, headers, "%s",
                    reply.stringify(true).c_str());
      goto done;
   }

  upload:
   if (!rbsid["user"]) {
      goto allfileserver;
   }
   username = rbsid["user"];
   user = &users[username];
   if (strcmp((ccp)user["bid"],bid.c_str())) {
      goto logout;
   }

   if (strstr(path, "/upload?")) {
      int maxThings = (bool)user["maxThings"]?
         user["maxThings"]:vhost["config"]["maxThings"];
      int maxThingPics = (bool)user["maxThingsPics"]?
         user["maxThingsPics"]:vhost["config"]["maxThingPics"];
      get_data_in_url(path, urlData);
      int thingId = atoi((ccp)urlData["thingId"]);
      int picId = atoi((ccp)urlData["picId"]);
      int fofst = atoi((ccp)urlData["offset"]);
      int chnkSz= atoi((ccp)urlData["chunkSize"]);
      int ttlSz = atoi((ccp)urlData["totalSize"]);
      int thngi = -1;
      // if (fofst!=0) {
      //    FFJSON& ptgs=user["pendingThings"];
      //    if(thingId!=(int)ptgs["thingId"]){
      //       mg_http_reply(c, 400, headers, "{%Q:%Q}", "error",
      //                     "noSuchThingId" );
      //       goto done;                  
      //    };
      //    picId=ptgs["picId"];
      //    thngi=ptgs["thngi"];
      //    goto gotThingId;
      // }
      FFJSON& uthings = user["things"];
      if (thingId < 0) {
         if (uthings && uthings.size>=maxThings) {
            ffl_notice (
               FPL_HTTPSERV,
               "user[\"things\"].size: %d", uthings.size
            );
            mg_http_reply(c, 400, headers, "{%Q:%Q}", "error",
                          "thingsAreAtMax" );
            goto done;
         }
         if (uthings && uthings.size) {
            thngi=uthings.size;
            thingId=(int)uthings[thngi-1]["id"]+1;
         } else {
            thngi=0;
            thingId=1;
         }
      } else {
         thngi=getIdChildInd(uthings, thingId);
         // int tSize = user["things"].size-1;
         // for (int i=(thingId<tSize?thingId:tSize); i>=0; --i) {
         //    if ((int)user["things"][i]["id"]==thingId) {
         //       thngi=i;
         //       break;
         //    }
         // }
         if (thngi<0) {
            mg_http_reply(c, 400, headers, "{%Q:%Q}", "error",
                          "noSuchThingId" );
            goto done;
         }
      }
     gotThingId:
//         ffl_notice(FPL_HTTPSERV, "Serving: %s", opts.root_dir);
      ffl_notice (
         FPL_HTTPSERV,
         "picId: %d, maxThingPics: %d, thingId: %d, thngi: %d",
         picId, maxThingPics, thingId, thngi
      );
      if (picId >= maxThingPics) {
         mg_http_reply(c, 400, headers, "{%Q:%Q}", "error",
                       "picsAreAtMax" );
         goto done;
      }
      string upldpth(opts.root_dir);
      upldpth += "/upload/";
      upldpth += username;
      upldpth += "/";
      upldpth += to_string(thingId);
      upldpth += ".";
      upldpth += to_string(picId);
      upldpth +=".jpg";
      ffl_notice(FPL_HTTPSERV, "receiving: %s", upldpth.c_str());
      if (fofst==0) {
         uthings[thngi]["id"]=thingId;
         if (!uthings[thngi]["user"]) {
            uthings[thngi]["user"].addLink(users, username);
         }
         FFJSON& ups = uthings[thngi]["pics"];
         ups[picId]["partial"] = true;
         // if (fofst+chnkSz<ttlSz) {
         //    FFJSON& ptgs=user["pendingThings"];
         //    ptgs["thingId"]=thingId;
         //    ptgs["picId"]=picId;
         //    ptgs["thngi"]=thngi;
         // }
      }
      char msg[30];
      sprintf(msg, "{\"thingId\":%d,\"picId\":%d", thingId, picId);
      mg_http_upload(
         c, hm, &mg_fs_posix, upldpth.c_str(), 2999999, msg);
      if (fofst+chnkSz >= ttlSz) {
//            user.erase("pendingThings");
         uthings[thngi]["pics"][picId].erase("partial");
         uthings[thngi]["pics"][picId]["ts"]=lepoch;
//            printf("pendingThings\n");
         users.save();
      }
   } else if (!strcmp(path, "/logout")) {
     logout:
      rbsid["user"]=nullFFJSON;
      mg_http_reply(c, 200, headers, "{%Q:%s}", "logout","true");
      rbs.save();
   } else if (strstr(path, "/update")) {
      FFJSON& user = users[username];
      if (strcmp((ccp)user["bid"],bid.c_str())) {
         mg_http_reply(c, 400, headers, "{%Q:%Q}", "error", "bidmismatch");
         goto done;
      }
      payload.init(cpld);
      if (payload["things"]) {
         FFJSON& cthings = payload["things"];
         FFJSON& uthings = user["things"];
         bool newthing=false;
         int id = 0;
         if (!(bool)uthings) {
            uthings.init("[]");
         }
         int j=0;
         for (int i=0; i<cthings.size; ++i) {
            if (!cthings[i])
               continue;
            if (i>uthings.size) {
               mg_http_reply(c, 400, headers, "{%Q:%Q}",
                             "error", "sizeExceeded");
               goto done;
            }
            FFJSON& cfname = cthings[i]["name"];
            string cname((ccp)cfname);
            if (!isValidThingName(cfname)) {
               mg_http_reply(c, 400, headers, "{%Q:%Q}",
                             "error", "invalidThingName");
               goto done;
            }
            j=getIdChildInd(uthings, (int)cthings[i]["id"]);
            bool locChanged = false;
            bool nameChanged = false;
            cthings[i].erase("user");
            vector<string> mstr;
            vector<uint> ina;
            if (j<0) {
               j=uthings.size;
               uthings[j]["id"] = j?(int)uthings[j-1]["id"]+1:1;
               FFJSON& ln = uthings[j]["user"].addLink(users, username);
               if (!ln)
                  delete &ln;
               uthings[j]["name"]=cthings[i]["name"];
               nameChanged=true;
               FFJSON& cloc = cthings[i]["location"];
               if (!isValidLocation(cloc)) {
                  mg_http_reply(c, 400, headers, "{%Q:%Q}",
                                "error", "invalidLocation");
                  goto done;
               } else {
                  uthings[j]["location"]=cloc;
                  locChanged=true;
               }
            } else {
               string uname(uthings[j]["name"]?(ccp)uthings[j]["name"]:"");
               tolower(cname);
               tolower(uname);
               if (strcmp(cname.c_str(),uname.c_str())) {
                  mstr = metaname(uname);
                  for (int k=0; k<mstr.size(); ++k) {
                     map<string, FFJSON*>::iterator it =
                        nameints->find(mstr[k]);
                     if (it->second->val.number==1) {
                        mitpos.erase(mitpos.find(&it->first));
                        fnameints->erase(mstr[k]);
                     } else {
                        --(*nameints)[mstr[k]]->val.number;
                     }
                  }
                  nameChanged=true;
                  uthings[j]["name"]=cthings[i]["name"];
               }
               FFJSON& cloc = cthings[i]["location"];
               FFJSON& uloc = uthings[j]["location"];
               if (!isValidLocation(cloc)) {
                  mg_http_reply(c, 400, headers, "{%Q:%Q}",
                                "error", "invalidLocation");
                  goto done;
               }
               if (((double)cloc[0]!=(double)uloc[0] ||
                    (double)cloc[1]!=(double)uloc[1])) {
                  mstr = metaname(uname);
                  uloc=cloc;
                  locChanged=true;
               }
               if (nameChanged||locChanged) {
                  ina = nametouint(mstr);
                  thnsTree.insert(uthings[j], ina, true);
               }
            }
            if (cthings[i]["details"]) {
               if (isValidThingDetails(cthings[i]["details"])) {
                  uthings[j]["details"]=cthings[i]["details"];
               } else {
                  mg_http_reply(c, 400, headers, "{%Q:%Q}",
                                "error", "invalidThingDetails");
                  goto done;
               }
            }
            if (nameChanged) {
               mstr=metaname(cname);
               for (int k=0; k<mstr.size(); ++k) {
                  map<string, FFJSON*>::iterator it =
                     nameints->find(mstr[k]);
                  if (it==nameints->end()) {
                     (*fnameints)[mstr[k]]=1;
                     it=nameints->find(mstr[k]);
                     mitpos[&it->first]=nameints->size()-1;
                  } else {
                     ++(*nameints)[mstr[k]]->val.number;
                  }
               }
               ina=nametouint(mstr);
            }
            if (locChanged||nameChanged) {
               thnsTree.insert(uthings[j], ina);
            }
            reply["things"][reply["things"].size]=&uthings[j];
         }
      }
      mg_http_reply(c, 200, headers, "%s", reply.stringify(true).c_str());
      users.save();
   } else if (strstr(path, "/owl")) {
      FFJSON& things = user["things"];
      FFJSON& smsgs = user["smsgs"];
      FFJSON& reps = user["reps"];
      int smind=smsgs.size;
      payload.init(cpld);
      FFJSON& fQs = payload["Qs"];
      FFJSON::Iterator it;
      long urts;
      long lmts;
      int i,j;
      if (!fQs) {
         goto rqs;
      }
      it = fQs.begin();
      while (it!=fQs.end()) {
         ccp tuser = (ccp)it;
         if (!strcmp(tuser,username)) {
            mg_http_reply(c, 400, headers, "{%Q:%Q}", "error", "yay!");
            goto done;
         }
         FFJSON::Iterator tit;
         if (tuser) {
            tit  = users.find(tuser);
         }
         if (!tuser || tit==users.end()) {
            mg_http_reply(c, 400, headers, "{%Q:%Q}", "error", "yay!");
            goto done;
         }
         FFJSON& tfuser = users[tuser];
         FFJSON& tfthings = tfuser["things"];
         tit = it->begin();
         while (tit!=it->end()) {
            ccp ctid = (ccp)tit;
            if (!ctid) {
               mg_http_reply(c, 400, headers, "{%Q:%Q}", "error", "yay!");
               goto done;
            }
            int tid = atoi(ctid);
            int tind = getIdChildInd(tfthings, tid);
            if (tind<0) {
               mg_http_reply(c, 400, headers, "{%Q:%Q}", "error", "yay!");
               goto done;
            }
            FFJSON& rmsgs = tfthings[tind]["rmsgs"];
            if (!rmsgs) {
               rmsgs.init("[]");
            }
            int rmind=rmsgs.size;
            smind=smsgs.size;
            int rmid=1;
            if (rmind) {
               rmid = (int)rmsgs[rmind-1]["id"]+1;
            }
            rmsgs[rmind]["id"]=rmid;
            rmsgs[rmind]["user"]=username;
            rmsgs[rmind]["msg"]=*tit;
            rmsgs[rmind]["ts"]=lepoch;
            rmsgs[rmind]["new"]=true;
            rmsgs[rmind]["smind"]=smind;
            smsgs[smind].init("[]");
            smsgs[smind][0]=tuser;
            smsgs[smind][1]=tid;
            smsgs[smind][2]=rmid;
            *tit=rmid;
            ++tit;
         }
         tfuser["lmts"]=lepoch;
         ++it;
      }
      payload["status"]=1;
     rqs:
      FFJSON& fRs = payload["Rs"];
      if (!fRs) {
         goto rrs;
      }
      it = fRs.begin();
      while (it!=fRs.end()) {
         ccp ctid = (ccp)it;
         if (!ctid) {
            mg_http_reply(c, 400, headers, "{%Q:%Q}", "error", "yay!");
            goto done;
         }
         int tid = atoi(ctid);
         int tind = getIdChildInd(things, tid);
         if (tind<0) {
            mg_http_reply(c, 400, headers, "{%Q:%Q}", "error",
                          "yay!");
            goto done;                     
         }
         FFJSON& rmsgs = things[tind]["rmsgs"];
         FFJSON::Iterator tit = it->begin();
         while (tit!=it->end()) {
            int mid = (int)*tit;
            mid = getIdChildInd(rmsgs, mid);
            if (mid<0) {
               mg_http_reply(c, 400, headers, "{%Q:%Q}", "error", "yay!");
               goto done;
            }
            rmsgs[mid].erase("new");
            ++tit;
         }
         ++it;
      }
      payload["status"]=1;
     rrs:
      FFJSON& frrs = payload["rrs"];
      if (!frrs) {
         goto news;
      }
      for (int i=0; i<frrs.size; ++i) {
         int smind = frrs[i];
         for (int j=0; j<reps.size; j+=2) {
            if ((int)reps[j]==smind) {
               reps.erase(j,j+2);
               break;
            }
         }
      }
      payload["status"]=1;
     news:
      urts = (long)rbsid["urts"];
      if (!user["lmts"]) {
         goto rnews;
      }
      lmts = (long)user["lmts"];
      if (urts>lmts) {
         goto rnews;
      }
      for (int i=0; i<things.size; ++i) {
         FFJSON& rmsgs = things[i]["rmsgs"];
         for (int j=0;j<rmsgs.size;++j) {
            FFJSON& msg = rmsgs[j];
            long mts = (long)msg["ts"];
            if (mts<urts) {
               continue;
            }
            payload["news"][to_string((int)things[i]["id"])]
               [to_string(j)]=msg;
         }
      }
      payload["status"]=1;
     rnews:
      if (!reps.size) {
         goto reps;
      }
      i=reps.size-1;
      lmts = (long)reps[i];
      if (urts>lmts) {
         goto reps;
      }
      j=0;
      do {
         --i;
         int smind = reps[i];
         FFJSON& smsg = smsgs[smind];
         FFJSON& tusrts = users[(ccp)smsg[0]]["things"];
         int tind = getIdChildInd(tusrts, (int)smsg[1]);
         FFJSON& trmsgs = tusrts[tind]["rmsgs"];
         int mind = getIdChildInd(trmsgs, (int)smsg[2]);
         FFJSON& rep=payload["rnews"][j];
         rep=smsg;
         rep[3]=trmsgs[mind]["rep"];
         rep[4]=smind;
         --i;++j;
         if (i<0) {
            break;
         }
         lmts=(long)reps[i];
      } while (urts<lmts);
     reps:
      FFJSON& fRps = payload["Reps"];
      if (!fRps) {
         goto owldone;
      }
      it = fRps.begin();
      while (it!=fRps.end()) {
         ccp ctid = (ccp)it;
         if (!ctid) {
            mg_http_reply(c, 400, headers, "{%Q:%Q}", "error", "yay!");
            goto done;
         }
         int tid = atoi(ctid);
         int tind = getIdChildInd(things, tid);
         if (tind<0) {
            mg_http_reply(c, 400, headers, "{%Q:%Q}", "error",
                          "yay!");
            goto done;                     
         }
         FFJSON& rmsgs = things[tind]["rmsgs"];
         FFJSON::Iterator tit = it->begin();
         while (tit!=it->end()) {
            int mid = stoi((ccp)tit);
            int mind = getIdChildInd(rmsgs, mid);
            if (mind<0) {
               mg_http_reply(c, 400, headers, "{%Q:%Q}", "error", "yay!");
               goto done;
            }
            smind=smsgs.size;
            rmsgs[mind]["rep"]=*tit;
            smsgs[smind].init("[]");
            smsgs[smind][0]="";
            smsgs[smind][1]=tid;
            smsgs[smind][2]=mid;
            FFJSON& tusr = users[(ccp)rmsgs[mind]["user"]];
            FFJSON& treps = tusr["reps"];
            if (!treps) {
               treps.init("[]");
            }
            treps[treps.size]=rmsgs[mind]["smind"];
            treps[treps.size]=lepoch;
            ++tit;
         }
         ++it;
      }
      payload["status"]=1;
     owldone:
      payload["status"]=1;
      rbsid["urts"]=lepoch;
      mg_http_reply(c, 200, headers, "%s",
                    payload.stringify(true).c_str());
      users.save();
      rbs.save();
   }
   goto done;
  fileserver:
   if (strstr(path, "/upload") ||
       strstr(path, "/tmp")) {
      goto done;
   }
  allfileserver:
   if (strstr(path, "/red")) {
      goto done;
   }
   mg_http_serve_dir(c, (mg_http_message*)ev_data, &opts);
  done:
   if (valgrind_test && !--valgrind_count)
      force_exit=true;
}

string myHandle (FFJSON& ffHttp) {
   FFJSON& fpath = ffHttp["path"];
   if (!fpath)
      return make_http_response("NaNa!");
   if (!ffHttp["host"])
      return "";
   subdomain=get_subdomain(ffHttp["host"]);
   ffl_notice(FPL_HTTPSERV, "subdomain: %s",subdomain.c_str());
   FFJSON& vhost = (bool)cfg["virtualWebHosts"][subdomain]?
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
      return ferryfair(ffHttp);
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

// void serve_static_fd (int fd, const string &path) {
//    string sp = sanitize_path(path);
//    string full = string((ccp)cfg["doc_root"]) + sp;
//    if (!full.empty() && full.back() == '/') full += "index.html";
//    struct stat st;
//    if (stat(full.c_str(), &st) == -1 || !S_ISREG(st.st_mode)) { send_404_fd(fd); return; }
//    ifstream ifs(full, ios::binary);
//    ostringstream bodyss;
//    bodyss << ifs.rdbuf();
//    string body = bodyss.str();
//    ostringstream hdr;
//    hdr << "HTTP/1.0 200 OK\r\n";
//    hdr << "Content-Type: " << mime_type_from_ext(full) << "\r\n";
//    hdr << "Content-Length: " << body.size() << "\r\n";
//    hdr << "Connection: close\r\n\r\n";
//    auto h = hdr.str();
//    write_all_fd(fd, h.data(), h.size());
//    write_all_fd(fd, body.data(), body.size());
// }

// void serve_static_ssl (SSL *ssl, int fd, const string &path) {
//    string sp = sanitize_path(path);
//    string full = string((ccp)cfg["doc_root"]) + sp;
//    if (!full.empty() && full.back() == '/') full += "index.html";
//    struct stat st;
//    if (stat(full.c_str(), &st) == -1 || !S_ISREG(st.st_mode)) { send_404_ssl(ssl); return; }
//    ifstream ifs(full, ios::binary);
//    ostringstream bodyss;
//    bodyss << ifs.rdbuf();
//    string body = bodyss.str();
//    ostringstream hdr;
//    hdr << "HTTP/1.0 200 OK\r\n";
//    hdr << "Content-Type: " << mime_type_from_ext(full) << "\r\n";
//    hdr << "Content-Length: " << body.size() << "\r\n";
//    hdr << "Connection: close\r\n\r\n";
//    auto h = hdr.str();
//    ssl_write_all(ssl, h.data(), (int)h.size());
//    ssl_write_all(ssl, body.data(), (int)body.size());
// }
// ---------------- Connection handlers ----------------

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
   auto r = ssl ? [ssl] (char* buf, size_t bufSize)->size_t {
      return SSL_read(ssl, buf, bufSize);
   } : [client_fd] (char* buf, size_t bufSize)->size_t {
      return read(client_fd, buf, bufSize);
   };
   parseHTTP(r, ffHttp);
   if (!ffHttp) {
      goto handledone;
   }
   ffl_info(HL, "%s %s %s %s fd=%d", (ccp)ffHttp["ip"], (ccp)ffHttp["version"],
            (ccp)ffHttp["method"], (ccp)ffHttp["path"], client_fd);
   string res = myHandle(ffHttp);
   if (!res.empty()) {
      ssl_write_all(ssl, res.data(), (int)res.size());
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
