// author: gowtham
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
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
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <set>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/socket.h>
#include <curl/curl.h>
#include <poll.h>
#include <mutex>                                                      
#include <sstream>                                                    

#include <FFJSON.h>
#include <logger.h>
#include <myconverters.h>
#include <FerryTimeStamp.h>
#include <mystdlib.h>
#include "https.h"
#include "ferryfair.h"

// In-memory store for push subscriptions (for demonstration purposes)
// In a real application, you would use a database.                   
static std::vector<std::string> s_subscriptions;                      
// Your VAPID public and private keys.                                
// Generate them once and keep them safe.                             
// You can use an online generator like                               
// https://www.stevesouders.com/bin/vapid.php                              
static const char *s_vapid_public_key = "YOUR_VAPID_PUBLIC_KEY";      
static const char *s_vapid_private_key = "YOUR_VAPID_PRIVATE_KEY";    

// For OpenSSL thread-safety in multi-threaded applications
static std::mutex *ssl_mutexes = nullptr;

static void lockingFunc (int mode, int n, const char *file, int line) {
   if (mode & CRYPTO_LOCK) {
      ssl_mutexes[n].lock();
   } else {
      ssl_mutexes[n].unlock();
   }
}

static unsigned long threadIdFunc (void) {                            
   // This is not guaranteed to be unique on all platforms, but is
   // efficient for OpenSSL's locking needs.                                 
    return (unsigned long)std::hash<std::thread::id>()(
       std::this_thread::get_id());
}

static void setupOsslLocking (void) {                             
   ssl_mutexes = new std::mutex[CRYPTO_num_locks()];
   CRYPTO_set_id_callback(threadIdFunc);
   CRYPTO_set_locking_callback(lockingFunc);
}

FFJSON cfg;

int child_exit_status = 0;
thread_local int tid = 0;

//#define hlDbg(str, ...) flDbg(HL, "tid: %d; "str, tid, __VA_ARGS__)

using namespace std;

typedef const char* ccp;

set<FFJSON*> pFSetToSave;
mutex setSavMtx;
atomic<bool> saveTxoStop{0};
mutex ipTracksMtx;
mutex TmOtFuncSetMtx;

void handleSigInt (int) {
   if (!atmcRunning) {
      exit(1);
   }
   atmcRunning = false;
   flDbg(HL, "interrupted! atmcRunning: %d", atmcRunning.load());
   flNtc(HL, "Shutting down...");
}

void handleSigpipe (int) {
   flDbg(HL, "sig pipe received.");
}

static void enableCoreDumps () {
   struct rlimit rl;
   rl.rlim_cur = RLIM_INFINITY;
   rl.rlim_max = RLIM_INFINITY;
   if (setrlimit(RLIMIT_CORE, &rl) != 0)
      perror("setrlimit(RLIMIT_CORE)");
}

static void moveCoreFile (pid_t pid) {
   const char* core_names[] = {
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
void ThreadPool::enqueue (function<void(int)> job) {
   {
      unique_lock<mutex> lk(mutex_);
      jobs_.push(move(job));
   }
   cv_.notify_one();
}
#ifdef _DEBUG
void ThreadPool::printThrdStats () {
   char buf[512];
   char* b = buf;
   for (int i=0; i<workers_.size(); ++i) {
      b += sprintf(b, "%d: %d, ", i, isRunning_[i]);
   }
   b-=2;
   *b='\0';
   flInf(HL, buf);
}
#else
void ThreadPool::printThrdStats () {}
#endif

void ThreadPool::start (size_t n) {
   for (size_t i=0; i<n; ++i) {
      workers_.emplace_back([this,i] () {
         tid = i;
         while (true) {
            function<void(int)> job; {
               unique_lock<mutex> lk(mutex_);
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
               isRunning_[tid]=1;
               job(tid);
               isRunning_[tid]=0;
               printThrdStats();
               --jc;
            } catch (const exception &e) {
               flErr(HL, "worker exception: %s", e.what());
            } catch (...) {
               flErr(HL, "worker exception: unknown");
            }
         }
      });
      isRunning_.push_back(0);
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
// write all bytes to fd
bool isValidMethod (char* buf) {
   static const char* methods = "get post";
   if (strcasestr(methods, buf)) {
      return true;
   }
   return false;
}
string htmlEscape (ccp s) {
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
void urlEscape (char* s) {
   char* c= s;
   while (*s!= '\0') {
      if (*s== '%' && *(s+1)!='\0' && *(s+2)!='\0') {
         s+=3;
			*c=*s;
         *s='\0';
         ++c;
         *c= (char)strtol(c, &s, 16);
         if (*s== '\0') {
            *s= *(c-1);
            *(c-1)=*c;
            continue;
         }
      }
      *c= *s;
      ++c;
      ++s;
   }
   *c= '\0';
}

using crd = function<ssize_t(char*, size_t)>;
using cwr = function<ssize_t(ccp, size_t)>;
void parseHost (crd read, FFJSON& host) {
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
void parseCookie (crd read, FFJSON& ffCookie) {
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

void parseAcceptEncoding (crd read, FFJSON& ffAEnc) {
   char c;
   string enc;
   while (read(&c, 1)>0) {
      switch (c) {
         case ',':
            ffAEnc[]= enc;
            flInfCntnu(HL, "%s,", enc.c_str());
            enc.clear();
         case ' ':
         case '\r':
            continue;
         case '\n':
            ffAEnc[]= enc;
            flInfCntnu(HL, "%s\n", enc.c_str());
            return;
         default:
            enc+= c;
            break;
      }
   }
   return;
}

void parseHTTP (crd read, FFJSON& ffHttp) {
   unsigned int i=0;
   unsigned int pairStartPin=i;
   char c;
   static const int bufSize = 1024;
   char buf[bufSize];
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
            uint8_t* pbuf= (uint8_t*)malloc(inL+1);
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
                     ffHttp["path"]= (ccp)buf;
                  } else {
                     urlEscape(buf+query);
                     ffHttp["query"][(ccp)buf]= (ccp)buf+query;
                     flInfCntnu(HL," %s: %s\n", buf, buf+query);
                     query= 1;
                  }
                  ci= 0;
                  ++spCnt;
                  continue;
               default:
                  break;
            }
         case '?':
            if (spCnt== 1) {
               if (query) {
                  flWrn(HL, "malformed query");
                  return;  
               }
               buf[ci]= '\0';
               flInfCntnu(HL, "path: %s\n", buf);
               ffHttp["path"]= (ccp)buf;
               ci= 0;
               query= 1;
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
               urlEscape(buf+query);
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
               // case "user-agent"_hash:
               //    parseUserAgent(read, fvalue);
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

static const set<string> noCacheMime {
	"video/mp2t", "application/vnd.apple.mpegurl"
};

int mkHttpRes (string& res, MkHttpArgs& args) {
	bool enc=false;
	if (res=="1") {
		res="";
		enc=true;
	}
   res+= "HTTP/1.0 "+ to_string(args.code)+ " "+ args.codeMsg+ "\r\n";
	string ctype(args.ctype);
   res+= "Content-Type: "+ ctype + "\r\n";
   res+= "Connection: close\r\n";
   if (!args.cchCtrl && !noCacheMime.contains(ctype)) {
      res+= "Cache-Control: public, max-age=3600\r\n";
   }
   if (args.addlHdrs) {
      res+= args.addlHdrs;
		res+= "\r\n";
	}
   if (!args.body)
      return -1;
   if (enc) {
      string gz = gzipCompress(args.body, args.bsz);
      res+= "Content-Encoding: gzip\r\n";
      res+= "Content-Length: "+ to_string(gz.size())+ "\r\n\r\n";
      res+= gz;
   } else {
      res+= "Content-Length: "+ to_string(args.bsz) + "\r\n\r\n";
      res+= args.body;
   }
   return -1;
}
int mkHttpRes (
   FFJSON& ffHttp, ccp body, ccp ctype, int bsz, const int code,
   ccp codeMsg, ccp addlHdrs
) {
   MkHttpArgs& mhArgs= ffHttp["resArgs"];
   mhArgs.body=  body;
   mhArgs.ctype= ctype;
   mhArgs.bsz= bsz;
   mhArgs.code= 200;
   mhArgs.codeMsg= codeMsg;
   if (!mhArgs.addlHdrs)
      mhArgs.addlHdrs= addlHdrs;
   else if (addlHdrs)
      flErr(HL, "addlHdrs: %s not added", addlHdrs);
   mhArgs.bsz= (bsz==-1)? body? strlen(body) : 0 : bsz;
	if (!ffHttp["res"]) {
		flDbg(HL, "1");
	}
	string& res= ffHttp["res"];
	FFJSON& accEnc= ffHttp["accept-encoding"];
	if (mhArgs.bsz>1024 && accEnc && accEnc["gzip"] &&
		 !strstr(mhArgs.ctype, "image")) {
		res="1";
	}
   return mkHttpRes(res, mhArgs);
}

int mkHttpRes (FFJSON& ffHttp, FFJSON& body) {
   mkHttpRes(ffHttp, nullptr, "text/json");
   string& res= ffHttp["res"];
   res+= "Content-Length: 00000000\r\n\r\n";
   int pos= res.length()-4;
   int size= res.length();
   body.stringify(res, true);
   size= res.length()- size;
   string sizestr= to_string(size);
   size= sizestr.length();
   for (int i=1; i<= size; ++i) {
      res[pos-i]= sizestr[size-i];
   }
   return -1;
}

enum ftype {
   FSFILE, SLINK, BLINK, DIR
};
static bool initIpBlocker () {
	string command = string("sudo ./createNFChain");
	int result = system(command.c_str());
	return (result == 0);
}
static bool blockIp (ccp ip) {
   string command = string("sudo ./blockHttpIp ") + ip;
   int result = system(command.c_str());
   return (result == 0);
}

static bool unblockIp (ccp ip) {
   string command = string("sudo ./unBlockHttpIp ") + ip;
   int result = system(command.c_str());
   return (result == 0);
}    

struct Entry {
   std::string name;
   ftype type;
   uintmax_t size;
   fs::file_time_type mtime;
};
string urlEncode (ccp s) {
   static const char* hex = "0123456789ABCDEF";
   string out;
   int sz = strlen(s);
   out.reserve(sz *3);
   for (int i=0; i<sz; ++i) {
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
string timeToString (const fs::file_time_type &ft) {
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

string getMimeType (const fs::path& path) {
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
      {".log",  "text/plain"},
      {".md",  "text/plain"},
      {".dts",  "text/plain"},
      {".ttf",  "font/ttf"},
      {".pdf",  "application/pdf"},
      {".xml",  "application/xml"},
      {".zip",  "application/zip"},
      {".gz",   "application/gzip"},
      {".tar",  "application/x-tar"},
      {".ts",  "video/mp2t"},
      {".m3u8",  "application/vnd.apple.mpegurl"}
		
      // add more as needed
   };

   auto ext = path.extension().string();
   // lowercase the extension
   for (auto &c : ext) c = static_cast<char>(tolower(c));
   flInf(HL, "ext: %s", ext.c_str());
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
typedef unordered_map<string, IpTrack_> IpTrksMp_;
IpTrksMp_ ipTracks;
struct TimeoutFunc_ {
   FTS_ timeout;
   virtual void func ()= 0;
};

struct CmpTout_ {
   bool operator () (const TimeoutFunc_* tf1, const TimeoutFunc_* tf2) const {
      return tf1->timeout < tf2->timeout;
   }
} cmpTout;
typedef set<TimeoutFunc_*, CmpTout_> TmOutSet_;
TmOutSet_ TmOtFuncSet(cmpTout);
struct TmOutIpUnblocker_:public TimeoutFunc_ {
   void func () override {
      unblockIp(ip);
   }
   ccp ip;
};

FTS_ oneMin = {60,0};
FTS_ halfMin = {30,0};

bool isNJsClient (FFJSON& ffHttp) {
   ccp ua = ffHttp["user-agent"];
   flInf(HL, ua);
   if (strstr(ua, "w3m") || strstr(ua, "nojs") || strstr(ua, "Dillo") ||
       strstr(ua, "Lynx") || strstr(ua, "Links") || strstr(ua, "Emacs")) {
      flDbg(HLL, "NJs");
      ffHttp["noJs"]=true;
      return true;
   }
   return false;
}

char* fileToStr (fs::path& fspath, char* buf) {
   ifstream in(fspath, ios::binary);
	if (!in) {
		return nullptr;
	}
	in.seekg(0, std::ios::end);
   streamsize size= in.tellg();
	if (size < 0) {
		return nullptr;
	}
   in.seekg(0, std::ios::beg);
   char* buffer= new char[size+ 1];
   if (in.read(buffer, size)) {
      buffer[size]= '\0';
      return buffer;
   }
   delete[] buffer;
   return nullptr;
}

int handleHttp (FFJSON& ffHttp) {
   MkHttpArgs mhArgs;
   ffHttp["resArgs"]= &mhArgs;
   FFJSON& fpath= ffHttp["path"];
   if (!fpath)
      return mkHttpRes(ffHttp, "NaNa!");
   FFJSON& host= ffHttp["host"];
   if (!host)
      return 0;
   ccp fqdn= host["fqdn"];
   string subdomain(fqdn,(int)host["subs"][0]);
   FFJSON& vhost= cfg["vhosts"][subdomain]?cfg["vhosts"][subdomain]:cfg;
   if (vhost["redirect"]) {
      char rhed[64];
      sprintf(rhed, "Location: %s\r\n", (ccp)vhost["redirect"]);
      return mkHttpRes(ffHttp, "", "text/plain", -1, 308, "Permanent Redirect",
                       rhed);
   }
   string path((ccp)vhost["rootdir"]);
   int plen= fpath.size;
   int res= 0;
   if (plen>1)
      path+= ((ccp)fpath)+1;
   else {
		mhArgs.cchCtrl= true;
      path+= "index.html";
	}
   flInf(HL, "serving %s", path.c_str());
   fs::path fspath(path);
   ffHttp["fspath"]= (void*)&fspath;
   res= ferryfair(ffHttp);
   if (res) {
      if (res== 1) {
         path= string((ccp)vhost["rootdir"]);
         path+= "/index.html";
         fspath= fs::path(path);
         goto serveIndex;
      } else if (res== 2) {
         goto iptrack;
      }
      return res;
   }
   if (fs::is_directory(fspath)) {
      path+= "/index.html";
      fs::path fsindex(path);
      if (fs::exists(fsindex)) {
         fspath= fsindex;
         mhArgs.cchCtrl= true;
         goto serveFile;
      }
      vector<Entry> entries;
      for (auto &de : fs::directory_iterator(fspath)) {
         Entry e;
         e.name= de.path().filename().string();
         e.type= de.is_directory()?ftype::DIR:de.is_symlink()?
            fs::exists(fs::status(de))?SLINK:BLINK:FSFILE;
         e.size= e.type!= FSFILE? 0: (de.is_regular_file()? de.file_size(): 0);
         e.mtime= e.type!= BLINK? de.last_write_time(): fs::file_time_type();
         entries.push_back(std::move(e));
      }
      sort(entries.begin(), entries.end(), [](auto &a, auto &b){
         return a.name < b.name;
      });
      string dirHtml= "<html><head><title>";
      dirHtml+= htmlEscape(path.c_str())+"</title></head><body><table>";
      dirHtml+= "<tr><th>Name</th><th>Size</th><th>Modified</th></tr>";
      for (auto &e : entries) {
         string disp= htmlEscape(
            (e.name+
             (e.type==DIR? "/": e.type== SLINK? "->": e.type== BLINK?"->x": "")
            ).c_str());
         string href= e.type!=BLINK?
            urlEncode((e.name + (e.type==DIR ? "/":"")).c_str()):"";
         string sizeStr= e.type!=FSFILE? "-":to_string(e.size);
         string mtime= timeToString(e.mtime);
         dirHtml+= "<tr>";
         dirHtml+= "<td><a href=\"" + href + "\">" + disp + "</a></td>";
         dirHtml+= "<td>" + sizeStr + "</td>";
         dirHtml+= "<td>" + mtime + "</td></tr>";
      }
      dirHtml+= "</table></body></html>";
      mhArgs.body= dirHtml.c_str();
      mhArgs.ctype= "text/html";
      return mkHttpRes(ffHttp);
   } else {
     serveIndex:
      flDbg(HL,"fspath: %s",fspath.c_str());
      if (!fs::exists(fspath))
         goto iptrack;
     serveFile:
      string ctype= getMimeType(fspath);
      mhArgs.ctype= ctype.c_str();
      string& res= ffHttp["res"];
      mkHttpRes(res, mhArgs);
      uint fsize= fs::file_size(fspath);
      res+= "Content-Length: "+ to_string(fsize)+ "\r\n\r\n";
      ifstream in(fspath);
      res.append(istreambuf_iterator<char>(in), istreambuf_iterator<char>());
      return -1;
   }
  iptrack:
   FTS_ now; now.update();
	ipTracksMtx.lock();
   IpTrack_& ipt = ipTracks[(ccp)ffHttp["ip"]];
	ipTracksMtx.unlock();
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
         TmOutIpUnblocker_* tmOtIpUnBlkr = new TmOutIpUnblocker_();
			ipTracksMtx.lock();
         IpTrksMp_::iterator it = ipTracks.find((ccp)ffHttp["ip"]);
			ipTracksMtx.unlock();
         tmOtIpUnBlkr->ip=it->first.c_str();
         tmOtIpUnBlkr->timeout=now+7200;
			TmOtFuncSetMtx.lock();
         TmOtFuncSet.insert(tmOtIpUnBlkr);
			TmOtFuncSetMtx.unlock();
      } else if (ipt.count <2) {
         ipt.firstReqTime=now;
      }
   }
   mhArgs.body="NaNa!";
   return mkHttpRes(ffHttp);
}
int waitForRead (int fd, int timeoutMs) {
    struct pollfd p = { fd, POLLIN, 0 };
    return poll(&p, 1, timeoutMs);
}

int waitForWrite (int fd, int timeoutMs) {
    struct pollfd p = { fd, POLLOUT, 0 };
    return poll(&p, 1, timeoutMs);
}

atomic<int> sslCount{0};
void handleConnection (int tid, struct sockaddr_in cli, int clientFd,
                       SSL* ssl = nullptr) {
   FFJSON ffHttp;
   char ip_str[INET_ADDRSTRLEN];
   inet_ntop(AF_INET, &cli.sin_addr, ip_str, sizeof(ip_str));
   ffHttp["ip"] = (ccp)ip_str;
   flInf(HL, "tid: %d, %s, %d------", tid, ip_str, clientFd);
   //makeNonBlocking(clientFd);
   string res;
   ffHttp["res"]= &res;
   crd nr = [clientFd] (char* buf, size_t bufSize)->ssize_t {
      return recv(clientFd, buf, bufSize, 0);
   };
   crd sr = [ssl] (char* buf, size_t bufSize)->ssize_t {
      return SSL_read(ssl, buf, bufSize);
   };
   crd rd = ssl ? sr : nr;
   crd rr = [&rd, clientFd] (char* buf, size_t bufSize)->ssize_t {
      int retry = cfg["readRetry"];
      static int retryMS = cfg["retryMS"];
     readagain:
      ssize_t r = rd(buf, bufSize);
      if (r<0 && retry>0) {
         if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            --retry;
            waitForRead(clientFd, retryMS);
            flDbgCntnu(HLL, "retry: %d, r: %zd, ", retry, r);
            goto readagain;
         }
      }
      return r;
   };
   parseHTTP(rr, ffHttp);
   if (!ffHttp["version"]) {
      goto handledone;
   }
   handleHttp(ffHttp);
   if (res.length()) {
      cwr nw = [clientFd] (ccp buf, size_t bufSize)->ssize_t {
         return send(clientFd, buf, bufSize, 0);
      };
      cwr sw = [ssl] (ccp buf, size_t bufSize)->ssize_t {
         return SSL_write(ssl, buf, bufSize);
      };
      cwr wd = ssl ? sw : nw;
      cwr rw = [&wd, clientFd] (ccp buf, size_t bufSize)->ssize_t {
          int retry = cfg["readRetry"];
          static int retryMS = cfg["retryMS"];
          size_t off = 0;
          while (off < bufSize) {
            writeagain:
             ssize_t w = wd(buf+off, bufSize - off);
             if (w<=0) {
                if (retry>0 && (errno==EAGAIN || errno==EINTR)) {
                   waitForWrite(clientFd, retryMS);
                   flDbgCntnu(HL, "fd: %d, w: %zd, e: %d %zd/%zd retry",
                              clientFd, w, errno, off, bufSize);
                   --retry;
                   goto writeagain;
                } else {
                   flDbg(HL, "fd: %d, write error, closing at %d",
                         clientFd, off);
                   return off;
                }
             }
             off += w;
          }
          return off;
       };
      rw(res.c_str(), res.length());
   }
   
  handledone:
   if (ssl) {
      SSL_shutdown(ssl);
      SSL_free(ssl);
		flDbg(HL, "ssl %p destroyed", ssl);
		--sslCount;
   }
   ::close(clientFd);
   flDbg(HL, "tid:%d: %d fd closed", tid, clientFd);
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
   const SSL_METHOD *method = TLS_server_method();
   SSL_CTX *ctx = SSL_CTX_new(method);
   if (!ctx) return nullptr;
   if (SSL_CTX_use_certificate_chain_file(
          ctx, cert_file.c_str()) <= 0) {
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
void acceptLoop (int listen_fd, ThreadPool& pool, SSL_CTX* ctx = nullptr) {
   while (atmcRunning) {
      struct sockaddr_in cli{};
      socklen_t sl = sizeof(cli);
      flDbg(HL, "ssl: %p, listening on %d...", ctx, listen_fd);
      int c = accept(listen_fd, (struct sockaddr*)&cli, &sl);
      if (c < 0) {
         flErr(HL, "accept failed: %s, ssl: %p", strerror(errno), ctx);
         if (errno == EINTR) {
            flNtc(HL, "ctx: %p, interrupted!", ctx);
            return;
         };
         continue;
      }
      flDbg(HL, "got %d...", c);

      SSL* ssl = nullptr;
      if (ctx) {
         struct timeval timeout;
         timeout.tv_sec = 5;  // 5-second timeout
         timeout.tv_usec = 0;
         if (setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))
             < 0) {
            flErr(HL, "Failed to set socket rcv timeout");
            ::close(c);
            continue;
         }
         if (setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout))
             < 0) {
            flErr(HL, "Failed to set socket snd timeout");
            ::close(c);
            continue;
         }
         ssl= SSL_new(ctx);
			flDbg(HL, "ssl %p created", ssl);
			++sslCount;
         SSL_set_fd(ssl, c);
         int r= SSL_accept(ssl);
         if (r<0) {
            flErr(HL, "tid %d: SSL accept failed: %d: %s", tid, r,
                  ERR_error_string(SSL_get_error(ssl, r), nullptr));
            SSL_shutdown(ssl);
            SSL_free(ssl);
				--sslCount;
            flDbg(HL, "ssl %p destroyed", ssl);
				::close(c);
            flDbg(HL, "tid:%d: %d fd closed", tid, c);
            continue;
         }
      }
       // make socket non-blocking
       int flags = fcntl(c, F_GETFL, 0);
       if (flags == -1) continue;
       fcntl(c, F_SETFL, flags | O_NONBLOCK);

       pool.enqueue([cli, ssl, c] (int tid) {
          handleConnection(tid, cli, c, ssl);
       });
    }
}

void saveTxo () {
   FFJSON* p;
   while (!saveTxoStop) {
      this_thread::sleep_for(chrono::milliseconds(2000));
		flDbg(HLL, "saving Txo");
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


void serveTimeouts () {
   FTS_ now;
   while (atmcRunning) {
      this_thread::sleep_for(chrono::milliseconds(2000));
		flDbg(HLL, "running timeouts");
      while (1) {
			TmOtFuncSetMtx.lock();
         TmOutSet_::iterator it = TmOtFuncSet.begin();
			if (it==TmOtFuncSet.end()) {
				TmOtFuncSetMtx.unlock();
			   break;
			}
         TmOtFuncSetMtx.unlock();
			TimeoutFunc_* topTmOtFunc = *it;
         now.update();
         if (topTmOtFunc->timeout < now || !atmcRunning) {
            topTmOtFunc->func();
				TmOtFuncSetMtx.lock();
            TmOtFuncSet.erase(it);
				TmOtFuncSetMtx.unlock();
            delete topTmOtFunc;
         } else {
            break;
         }
      }
   }
}
// ---------------- CLI and main ----------------
void usage_and_exit (const char *p) {
   ffl_err(HL, "Usage: %s --cert cert.pem --key key.pem [--http-port N]"
           " [--https-port N] [--docroot PATH] [--threads N]", p);
   exit(1);
}
// MUST run before any thread creation
static void disableSigpipe () {
   struct sigaction sa{};
   sa.sa_handler = SIG_IGN;
   sigaction(SIGPIPE, &sa, NULL);

   sigset_t set;
   sigemptyset(&set);
   sigaddset(&set, SIGPIPE);
   pthread_sigmask(SIG_BLOCK, &set, NULL);
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
   disableSigpipe();
   flNtc(HL, "Starting server. docroot=%s threads=%d",
              (ccp)cfg["rootdir"], (int)fCfgThrdCnt);

   curl_global_init(CURL_GLOBAL_DEFAULT);
   initIpBlocker();
	
   tpoolPtr = new ThreadPool((int)fCfgThrdCnt);
   initFerryFair(cfg["vhosts"]["www"]);

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
      string((ccp)cfg["ca"]), string((ccp)cfg["key"]));
   if (!sslCtx) {
      ffl_err(HL, "Failed to create SSL_CTX");
      return 1;
   }
   thread t1([httpFd] () {
      acceptLoop(httpFd, *tpoolPtr);
   });
   thread t2([httpsFd, &sslCtx] () {
      acceptLoop(httpsFd, *tpoolPtr, sslCtx);
   });
   thread saveTxoT(saveTxo);
   thread serveTmOtT(serveTimeouts);
   flDbg(HL, "main sleeping..");
   while (atmcRunning) {
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
	uninitFerryFair();
	flDbg(HL, "deleting thread pool");
   delete tpoolPtr;
   curl_global_cleanup();
   saveTxoStop=true;
   flDbg(HL, "saving pending files");
   saveTxoT.join();
   serveTmOtT.join();
   return 0;
}
int main (int argc, char **argv) {
   fflAllowedType= (FF_LOG_TYPE) (FFL_ERR | FFL_NOTICE | FFL_DEBUG |
                                  FFL_INFO | FFL_WARN);
   fflAllowedBlks= (uint)(HL|FL);
   setupOsslLocking();
   cfg.init("file://http.ffjson|OBJECT");
   flDbg(HL, "%s\n", cfg.prettyString().c_str());
   flDbg(HL, "EAGAIN(%zd) EINTR(%zd) EINVAL(%zd)\n",
         EAGAIN, EINTR, EINVAL);
   signal(SIGINT, handleSigInt);
   signal(SIGPIPE, SIG_IGN);
   //signal(SIGPIPE, handleSigpipe);
   if (cfg["daemon"]) {
      enableCoreDumps();
      struct stat statbuf;
      FTS_ ts;
      ts.update();
      char log[64];
      sprintf(log, "httpd-%zu.log", ts.tv_sec);
      int ferr = open (
         log, O_CREAT | O_WRONLY | O_TRUNC, 0600);
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
	flDbg(HL,"sslCount: %d", sslCount.load());
   flNtc(HL, "bye!----------");
   return 0;
}
