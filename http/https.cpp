// author: gowtham
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/resource.h>
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
#include <cstdlib>
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
                                            FFL_INFO | FFL_WARN);
unsigned int fflAllowedBlks = (uint)(HL|FL|HSL);
thread_local int tid = 0;

using namespace std;
namespace fs = std::filesystem;

typedef const char* ccp;

set<FFJSON*> pFSetToSave;
mutex setSavMtx;
atomic<bool> saveTxoStop{0};

void handle_sigint (int) {
   if (!g_running) {
      exit(1);
   }
   g_running = false;
   flDbg(HL, "interrupted! g_running: %d", g_running.load());
   flNtc(HL, "Shutting down...");
}

static void enableCoreDumps () {
   struct rlimit rl;
   rl.rlim_cur = RLIM_INFINITY;
   rl.rlim_max = RLIM_INFINITY;
   if (setrlimit(RLIMIT_CORE, &rl) != 0)
      perror("setrlimit(RLIMIT_CORE)");
}

static void moveCoreFile (pid_t pid) {
   const char *core_names[] = {
      "core", "core.dump", "core.%d", "core.%d.dump"
   };
   for (const char *pattern : core_names) {
      char src[64], dst[128];
      snprintf(src, sizeof(src), pattern, pid);

      struct stat st;
      if (stat(src, &st) == 0) {
         time_t now = time(nullptr);
         struct tm tm;
         localtime_r(&now, &tm);

         snprintf(dst, sizeof(dst),
                  "core.httpd.%d.%04d%02d%02d_%02d%02d%02d.dump",
                  pid, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);

         if (rename(src, dst) == 0)
            flNtc(HL, "core file renamed to: %s\n", dst);
         else
            perror("rename core file");
         return;
      }
   }
}

string gzipCompress (
   ccp data, int dsz, int level = Z_BEST_COMPRESSION) {
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
   
   zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));
   zs.avail_in = dsz;
   
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

ThreadPool::ThreadPool (size_t n) {
   start(n);
};
ThreadPool::~ThreadPool () {
   stop();
};
void ThreadPool::init (size_t n) {
   if (!started) {
      start(n);
      started = true;
   }
}
void ThreadPool::enqueue(function<void()> job) {
   {
      unique_lock<mutex> lk(mutex_);
      jobs_.push(move(job));
   }
   cv_.notify_one();
}

void ThreadPool::start (size_t n) {
   for (size_t i=0; i<n; ++i) {
      workers_.emplace_back([this,i] () {
         tid = i;
         while (true) {
            function<void()> job; {
               unique_lock<mutex> lk(mutex_);
               flDbg(HL, "jobs in queue: %d", jobs_.size());
               if (jobs_.empty() && !jc) {
                  cvJoin_.notify_all();
               }
               cv_.wait(lk, [this] {
                  return stopping_ || !jobs_.empty();
               });
               if (stopping_ && jobs_.empty()) {
                  flDbg(HL, "tid: %d, exit", tid);
                  return;
               }
               job = move(jobs_.front());
               jobs_.pop();
            }
            try {
               ++jc;
               flDbg(HL, "tid: %d, start!", tid);
               job();
               flDbg(HL, "tid: %d, done!", tid);
               --jc;
            } catch (const exception &e) {
               flErr(HL, "worker exception: %s", e.what());
            } catch (...) {
               flErr(HL, "worker exception: unknown");
            }
         }
      });
   }
}

void ThreadPool::stop () {
   {
      unique_lock<mutex> lk(mutex_);
      stopping_ = true;
   }
   cv_.notify_all();
   for(auto &t : workers_)
      if (t.joinable())
         t.join();
}
void ThreadPool::join () {
   unique_lock<mutex> lk(mutex_);
   cvJoin_.wait(lk, [this] {
      return jobs_.empty();
   });
}

ThreadPool* tpoolPtr;
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
   FFJSON& fqdn = host["fqdn"];
   FFJSON& subs = host["subs"];
   subs.init("[]");
   bool port = false;
   int ci=0;
   while (read(&c, 1)>0) {
      switch (c) {
         case '\r':
            continue;
         case '\n':
            flInfCntnu(HL,"%s\n", buf.c_str());
            fqdn=buf;
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
            if (buf->length())
               while (buf->back()==' ')
                  buf->pop_back();
            ffCookie[key]=value;
            flInfCntnu(HL,"%s=%s",key.c_str(),value.c_str());
            key.clear(); value.clear();
            if (c=='\n') {
               flInfCntnu(HL,"\n");
               return;
            } else {
               flInfCntnu(HL,";");
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
            flInfCntnu(HL, "%s,", enc.c_str());
            enc.clear();
         case ' ':
         case '\r':
            continue;
         case '\n':
            ffAEnc[]=enc;
            flInfCntnu(HL, "%s\n", enc.c_str());
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
   static const int bufSize = 1024;
   char buf[bufSize];
   flInf(HL, "request: ");
   int li=0;
   int spCnt=0;
   int ci=0,hend = 0,bodyBegin=0,query=0;
   while (true) {
      if (bodyBegin) {
         if (ffHttp["content-length"]) {
            int inL = atoi((ccp)ffHttp["content-length"]);
            ffHttp["content-length"]=inL;
            if (!inL)
               return;
            uint8_t* pbuf = new uint8_t[inL+1];
            ssize_t r = read((char*)pbuf, inL);
            if (r<=0) {
               flDbg(HL,"end r: %zd", r);
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
         if (ci>=bufSize) {
            flWrn(HL, "HTTP Header exceeded bufSize: %d", bufSize);
            return;
         }
         ssize_t r = read(&c, 1);
         if (r<=0) {
            return;
         }
      }
      switch (c) {
         case '\n':
            buf[ci]='\0';
            if (!li) {
               flInfCntnu(HL, "version: %s\n", buf);
               ffHttp["version"]=(ccp)buf;
            } else if (hend) {
               ffHttp[(ccp)buf]=(ccp)(buf+hend);
               flInfCntnu(HL, "%s\n", (ccp)(buf+hend));
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
                  flInfCntnu(HL, "method: %s\n", buf);
                  ffHttp["method"]=(ccp)buf;
                  ci=0;
                  ++spCnt;
                  continue;
               case 1:
                  buf[ci]='\0';
                  if (!query) {
                     flInfCntnu(HL, "path: %s\n", buf);
                     ffHttp["path"]=(ccp)buf;
                  } else {
                     ffHttp["query"][(ccp)buf]=(ccp)buf+query;
                     flInfCntnu(HL," %s: %s\n", buf, buf+query);
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
               if (query) {
                  flWrn(HL, "malformed query");
                  return;  
               }
               buf[ci]='\0';
               flInfCntnu(HL, "path: %s\n", buf);
               ffHttp["path"]=(ccp)buf;
               ci=0;
               query=1;
               flInfCntnu(HL, "query:");
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
               flInfCntnu(HL," %s: %s,", buf, buf+query);
               query=1;
               ci=0;
               continue;
            }
            break;
         case ':':
            if (li && !hend) {
               buf[ci]='\0';
               flInfCntnu(HL,"%s: ",buf);
               tolower((ccp)buf);
               size_t key = fnv1a(buf);
               FFJSON& fvalue = ffHttp[(ccp)buf];
               switch (key) {
                  case "cookie"_hash:
                     parseCookie(read, fvalue);
                     break;
                  case "host"_hash:
                     parseHost(read, fvalue);
                     break;
                  case "accept-encoding"_hash:
                     parseAcceptEncoding(read, fvalue);
                     break;
                  default:
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

string mkHttpRes (
   FFJSON& ffHttp, ccp body, ccp ctype, int bsz, const int code,
   ccp codeMsg, ccp addlHdrs
) {
   MkHttpArgs ma(&ffHttp, body, ctype, bsz, code, codeMsg, addlHdrs);
   return mkHttpRes(ma);
}
string mkHttpRes (MkHttpArgs& args) {
   ostringstream oss;
   oss << "HTTP/1.0 " << args.code << " " << args.codeMsg << "\r\n";
   oss << "Content-Type: " << args.ctype << "\r\n";
   oss << args.addlHdrs;
   oss << "Connection: close\r\n";
   if (!args.cchCtrl) {
      oss << "Cache-Control: public, max-age=3600\r\n";
   }
   int ocl = args.bsz==-1?strlen(args.body):args.bsz;
   FFJSON& accEnc = (*args.ffHttp)["accept-encoding"];
   if (ocl>1024 && accEnc && accEnc["gzip"] &&
       !strstr(args.ctype,"image")) {
      string gz = gzipCompress(args.body, ocl);
      oss << "Content-Encoding: gzip\r\n";
      oss << "Content-Length: " << gz.size() << "\r\n\r\n";
      oss << gz;
   } else {
      oss << "Content-Length: " << ocl << "\r\n\r\n";
      oss.write(args.body,ocl);
   }
   return oss.str();
}

enum ftype {
   FSFILE, SLINK, BLINK, DIR
};
static bool blockIp (ccp ip) {
   string command = string("blockHttpIp ") + ip;
   int result = system(command.c_str());
   return (result == 0);
}

static bool unblockIp (ccp ip) {
   string command = string("unBlockHttpIp ") + ip;
   int result = system(command.c_str());
   return (result == 0);
}    

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
string time_to_string (const fs::file_time_type &ft) {
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
      {".md",  "text/plain"},
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

struct IpTrack_ {
   FTS_ firstReqTime;
   int count = 0;
};
unordered_map<string, IpTrack_> ipTracks;
FTS_ oneMin = {60,0};
FTS_ halfMin = {30,0};

string httpHandle (FFJSON& ffHttp) {
   FFJSON& fpath = ffHttp["path"];
   if (!fpath)
      return mkHttpRes(ffHttp, "NaNa!");
   FFJSON& host = ffHttp["host"];
   if (!host)
      return "";
   ccp fqdn = host["fqdn"];
   string subdomain(fqdn,(int)host["subs"][0]);
   FFJSON& vhost = cfg["vhosts"][subdomain]?cfg["vhosts"][subdomain]:cfg;
   if (vhost["redirect"]) {
      char rhed[64];
      sprintf(rhed, "Location: %s\r\n", (ccp)vhost["redirect"]);
      return mkHttpRes(ffHttp, "", "text/plain", -1, 308, "Permanent Redirect",
                       rhed);
   }
   string path((ccp)vhost["rootdir"]);
   int plen = fpath.size;
   string res;
   MkHttpArgs mhArgs;
   path+="/";
   if (plen>1)
      path+=((ccp)fpath)+1;
   else
      path+="index.html";
   flInf(HL,"serving %s", path.c_str());
   fs::path fspath(path);
   res = ferryfair(ffHttp);
   if (res.length()) {
      if (res=="1") {
         path = string((ccp)vhost["rootdir"]);
         path += "/index.html";
         fspath=fs::path(path);
         goto serveIndex;
      }
      return res;
   }
   mhArgs.ffHttp=&ffHttp;
   if (fs::is_directory(fspath)) {
      path += "/index.html";
      fs::path fsindex(path);
      if (fs::exists(fsindex)) {
         fspath=fsindex;
         mhArgs.cchCtrl=true;
         goto serveFile;
      }
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
      mhArgs.body=dirHtml.c_str();
      mhArgs.ctype = "text/html";
      return mkHttpRes(mhArgs);
   } else {
     serveIndex:
      if (!fs::exists(fspath))
         goto iptrack;
     serveFile:
      ifstream reqFile(path);
      ostringstream resStr;
      resStr << reqFile.rdbuf();
      string res = resStr.str();
      mhArgs.body=res.c_str();
      mhArgs.bsz=res.length();
      mhArgs.ctype = get_mime_type(fspath).c_str();
      return mkHttpRes(mhArgs);
   }
  iptrack:
   FTS_ now; now.update();
   IpTrack_& ipt = ipTracks[(ccp)ffHttp["ip"]];
   flNtc(HL, "track: %s requested %s %d times",
         (ccp)ffHttp["ip"], (ccp)ffHttp["path"], ipt.count);
   if (!ipt.firstReqTime) {
      ipt.firstReqTime=now;
   }
   ++ipt.count;
   if (halfMin < (now-ipt.firstReqTime)) {
      if (ipt.count>7) {
         flNtc(HL, "blocking %s", (ccp)ffHttp["ip"]);
         blockIp((ccp)ffHttp["ip"]);
      } else if (ipt.count <2) {
         ipt.firstReqTime=now;
      }
   }
   mhArgs.body="NaNa!";
   return mkHttpRes(mhArgs);
}

void handleConnection (struct sockaddr_in cli, int clientFd,
                        SSL* ssl = nullptr) {
   if (ssl && SSL_accept(ssl) <= 0) {
      ffl_err(HL, "SSL accept failed: %s",
              ERR_error_string(ERR_get_error(), nullptr));
      SSL_shutdown(ssl);
      SSL_free(ssl);
      ::close(clientFd); return;
   }
   FFJSON ffHttp;
   char ip_str[INET_ADDRSTRLEN];
   inet_ntop(AF_INET, &cli.sin_addr, ip_str, sizeof(ip_str));
   ffHttp["ip"] = (ccp)ip_str;
   
   //makeNonBlocking(clientFd);
   crdwr nr = [clientFd] (char* buf, size_t bufSize)->size_t {
      return recv(clientFd, buf, bufSize, 0);
   };
   crdwr sr = [ssl] (char* buf, size_t bufSize)->size_t {
      return SSL_read(ssl, buf, bufSize);
   };
   crdwr rd = ssl ? sr : nr;
   crdwr rr = [&rd, clientFd] (char* buf, size_t bufSize)->size_t {
      int retry = cfg["readRetry"];
      static int retryMS = cfg["retryMS"];
     readagain:
      ssize_t r = rd(buf, bufSize);
      if (r<0 && retry>0) {
         flDbg(HL, "r: %zd", r);
         if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            --retry;
            flDbg(HL, "retry %d", retry);
            this_thread::sleep_for(chrono::milliseconds(retryMS));
            flDbg(HL, "retry %d woke", retry);
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
   flInf(HL, "%s %s %s %s fd=%d", (ccp)ffHttp["ip"], (ccp)ffHttp["version"],
         (ccp)ffHttp["method"], (ccp)ffHttp["path"], clientFd);
   res = httpHandle(ffHttp);
   if (!res.empty()) {
      crdwr nw = [clientFd] (char* buf, size_t bufSize)->size_t {
         size_t total = 0;
         // make socket non-blocking
         int flags = fcntl(clientFd, F_GETFL, 0);
         if (flags == -1) return false;
         fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);

         while (total < bufSize) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(clientFd, &wfds);

            static struct timeval tv = {
               10, 0
            };
            int rv = select(clientFd + 1, nullptr, &wfds, nullptr, &tv);
            if (rv == 0) {
               flDbg(HSL, "timedOut");
               return total; // timeout
            } else if (rv < 0) {
               if (errno == EINTR) continue;
               perror("select");
               flDbg(HSL, "selectErr");
               return total;
            }

            if (FD_ISSET(clientFd, &wfds)) {
               ssize_t sent = send(clientFd, buf + total, bufSize - total, 0);
               if (sent > 0) {
                  total += sent;
               } else if (sent < 0) {
                  if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                  perror("send");
                  flDbg(HSL, "sendErr");
                  return total;
               } else {
                  flDbg(HSL, "connectionClose");
                  return total;
               }
            }
         }
         return total;
      };
      crdwr sw = [ssl] (char* buf, size_t bufSize)->size_t {
         return SSL_write(ssl, buf, bufSize);
      };
      crdwr wd = ssl ? sw : nw;
      crdwr rw = [&wd, clientFd] (char* buf, size_t bufSize)->size_t {
         int retry = cfg["readRetry"];
         static int retryMS = cfg["retryMS"];
         size_t off = 0;
         while (off < bufSize) {
           writeagain:
            ssize_t w = wd(buf+off, bufSize - off);
            if (w<=0) {
               flDbg(
                  HL, "fd: %d, write socket error: %d(%s)@%zd/%zd", clientFd,
                  errno, strerror(errno), off , bufSize);
               if (retry>0 && (errno==EAGAIN || errno==EINTR)) {
                  flDbg(HL, "fd: %d, w: %zd", clientFd, w);
                  --retry;
                  this_thread::sleep_for(chrono::milliseconds(retryMS));
                  flDbg(HL, "fd: %d, write retry %d woke",
                            clientFd, retry);
                  goto writeagain;
               } else {
                  flDbg(HL,"fd: %d, write error, closing at %d",
                            clientFd, off);
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
   ::close(clientFd);
   flDbg(HL, "%d fd closed", clientFd);
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
   flInf(HL, "listening on %d", port);
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
      flDbg(HL, "listening on %d...", listen_fd);
      int c = accept(listen_fd, (struct sockaddr*)&cli, &sl);
      if (c < 0) {
         if (errno == EINTR) {
            return;
         };
         ffl_err(HL, "accept failed: %s", strerror(errno));
         continue;
      }
      SSL* ssl = nullptr;
      if (ctx) {
         ssl = SSL_new(ctx);
         SSL_set_fd(ssl, c);
      }
      flDbg(HL, "got %d...", c);
      pool.enqueue([cli, ssl, c](){
         handleConnection(cli, c, ssl);
      });
   }
}

void saveTxo () {
   FFJSON* p;
   while (!saveTxoStop) {
      this_thread::sleep_for(chrono::milliseconds(2000));
      while (!pFSetToSave.empty()) {
         setSavMtx.lock();
         set<FFJSON*>::iterator it = pFSetToSave.begin();
         p=*it;
         pFSetToSave.erase(it);
         setSavMtx.unlock();
         p->save();
      }
   }
}

// ---------------- CLI and main ----------------
void usage_and_exit (const char *p) {
   ffl_err(HL, "Usage: %s --cert cert.pem --key key.pem [--http-port N]"
           " [--https-port N] [--docroot PATH] [--threads N]", p);
   exit(1);
}

int run () {
   if (cfg["daemon"]) {
      FTS_ ts;
      ts.update();
      char log[64];
      sprintf(log, "https-%zu.log", ts.tv_sec);
      int ferr = open(log, O_WRONLY | O_CREAT, 0600);
      if (ferr < 0) {
         flErr(HL, "couldn't open https.log");
         return 1;
      }
      dup2(ferr, 1);
      dup2(ferr, 2);
      close(ferr);
   }
   FFJSON& fCfgThrdCnt = cfg["threadCount"];
   if ((int)fCfgThrdCnt<=0)
      fCfgThrdCnt=2*thread::hardware_concurrency();
   if (!(cfg["cert"] && cfg["key"] && cfg["ca"])) {
      flErr(HL, "improper cfg");
      return 0;
   }

   flNtc(HL, "Starting server. docroot=%s threads=%d",
              (ccp)cfg["rootdir"], (int)fCfgThrdCnt);

   tpoolPtr = new ThreadPool((int)fCfgThrdCnt);
   initFerryFair(cfg);

   int httpFd = create_listen_socket((uint16_t)(int)cfg["httpPort"]);
   if (httpFd < 0) {
      perror("http bind");
      return 1;
   }
   int httpsFd = create_listen_socket((uint16_t)(int)cfg["httpsPort"]);
   if (httpsFd < 0) {
      perror("https bind");
      return 1;
   }

   SSL_CTX* sslCtx = create_ssl_ctx(
      string((ccp)cfg["cert"]), string((ccp)cfg["key"]));
   if (!sslCtx) {
      ffl_err(HL, "Failed to create SSL_CTX");
      return 1;
   }
   thread t1([httpFd] () {
      accept_loop(httpFd, *tpoolPtr);
   });
   thread t2([httpsFd, &sslCtx] () {
      accept_loop(httpsFd, *tpoolPtr, sslCtx);
   });
   thread saveTxoT(saveTxo);
   flDbg(HL, "main sleeping..");
   while (g_running) {
      this_thread::sleep_for(chrono::milliseconds(2000));
   }

   ::shutdown(httpFd, SHUT_RDWR);
   ::shutdown(httpsFd, SHUT_RDWR);
   ::close(httpFd);
   ::close(httpsFd);
   flDbg(HL,"joining t1");
   if (t1.joinable()) t1.join();
   if (t2.joinable()) t2.join();
   flDbg(HL,"freeing ssl_ctx");
   SSL_CTX_free(sslCtx);
   flDbg(HL, "deleting thread pool");
   delete tpoolPtr;
   saveTxoStop=true;
   flDbg(HL, "saving pending files");
   saveTxoT.join();
   return 0;
}
int main (int argc, char **argv) {
   cfg.init("file://http.ffjson|OBJECT");
   flDbg(HL, "%s\n", cfg.prettyString().c_str());
   flDbg(HL, "EAGAIN(%zd) EINTR(%zd) EINVAL(%zd)\n",
             EAGAIN, EINTR, EINVAL);
   signal(SIGINT, handle_sigint);
   signal(SIGPIPE, SIG_IGN);
   if (cfg["daemon"]) {
      enableCoreDumps();
      struct stat statbuf;
      int stat_r = stat("httpd.log", &statbuf);
      int ferr = open (
         "httpd.log", O_CREAT | O_WRONLY | O_TRUNC, 0600);
      dup2(ferr, 1);
      dup2(ferr, 2);
      close(ferr);
      flInf(HL, "forking a http server...");
     createChild:
      pid_t pid = fork();
      if (pid) {
         //monitor child
         int status;
         flInf(HL, "forked! waiting on http server to exit.");
         waitpid(pid, &status, 0);
         if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            flNtc(HL, "server [%d] terminated by signal %d (%s)",
                  pid, sig, strsignal(sig));

            if (sig == SIGSEGV) {
               moveCoreFile(pid);
               flErr(HL, "reforking after crash...");
               goto createChild;  // restart loop
            }
         }
         if (WIFEXITED(status)) {
            flNtc(HL, "server(pid:[%d]) exited normally with code %d\n",
                  pid, WEXITSTATUS(status));
         }
      } else {
         run();
      }
   } else {
      run();
   }
   flNtc(HL, "bye!------------------------------------------------");
   return 0;
}
