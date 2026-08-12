#ifndef HTTPS
#define HTTPS

#include <functional>
#include <filesystem>
#include <queue>
#include <thread>
#include <shared_mutex>
#include <condition_variable>
#include <FFJSON.h>
#include <FerryTimeStamp.h>

#define Txo FFJSON
#define let auto;

namespace fs = std::filesystem;

extern FFJSON cfg;
enum HTTPLOG {
   HL = 1<<11,
   FL = 1<<12,
   SM = 1<<15,
   FLL = 1<<16,
   HSL = 1<<17,
   HLL = 1<<18,
   HLLL = 1<<19
};

static ccp jsonMime= "application/json";
static ccp txtMime= "text/plain";
static ccp htmlMime= "text/html";
static ccp okStr= "OK";
static atomic<bool> atmcRunning{true};

struct MkHttpArgs {
   ccp body;
   ccp ctype;
   int bsz;
   int code;
   ccp codeMsg;
   ccp addlHdrs;
   bool cchCtrl;
   ccp insCntnt;
   int insAt;
   MkHttpArgs (
      ccp body, ccp ctype = txtMime, int bsz=-1,
      const int code = 200, ccp codeMsg = okStr, ccp addlHdrs = nullptr) :
      body(body), ctype(ctype), bsz(bsz), code(code),
      codeMsg(codeMsg), addlHdrs(addlHdrs) {};
   MkHttpArgs ():body(nullptr), ctype(txtMime), bsz(-1), code(200),
                 codeMsg(okStr), addlHdrs(nullptr), cchCtrl(false),
                 insCntnt(nullptr), insAt(0){};
   void init () {
      body= nullptr;
      ctype= htmlMime;
      bsz= -1;
      int code= 200;
      codeMsg= okStr;
      addlHdrs= nullptr;
      cchCtrl= false;
   }
};

char* fileToStr (fs::path& fspath, char* buf= nullptr);
int mkHttpRes (string& res, MkHttpArgs& args);
int mkHttpRes (
   FFJSON& ffHttp, ccp body= nullptr, ccp ctype= "text/plain", int bsz= -1,
   const int code= 200, ccp codeMsg= "OK", ccp addlHdrs= nullptr);
int mkHttpRes (FFJSON& ffHttp, FFJSON& body);
inline int mkHttpRes (
   FFJSON& ffHttp, string body, ccp ctype = "text/plain",
   const int code = 200, ccp codeMsg = "OK", ccp addlHdrs = "") {
   return mkHttpRes(ffHttp, body.c_str(), ctype, body.length(), code, codeMsg,
                    addlHdrs);
}
class ThreadPool {
public:
   ThreadPool () {};
   ThreadPool (size_t n);
   ~ThreadPool ();
   void init (size_t n);
   void enqueue(function<void(int)> job);
   void join ();
   void printThrdStats ();

private:
   vector<thread> workers_;
   vector<char> isRunning_;
   queue<function<void(int)>> jobs_;
   mutex mutex_;
   condition_variable cv_;
   condition_variable cvJoin_;
   bool stopping_ = false;
   bool started;
   atomic<int> jc = {0};
   void start (size_t n);

   void stop ();
};
extern thread_local int tid;
extern ThreadPool* tpoolPtr;
extern set<FFJSON*> pFSetToSave;
extern mutex setSavMtx;
extern atomic<bool> saveTxoStop;
constexpr uint32_t fnv1a (const char *s, uint32_t hash = 2166136261u) {
   return (*s == 0) ? hash : fnv1a(s + 1, (hash ^ uint32_t(*s)) * 16777619u);
}
constexpr uint32_t operator ""_hash (const char *s, size_t) {
   return fnv1a(s);
}
static map<FFJSON*, shared_mutex> ffFileMtx;
#endif
