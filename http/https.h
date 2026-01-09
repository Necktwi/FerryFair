#ifndef HTTPS
#define HTTPS

#include <functional>
#include <filesystem>
#include <queue>
#include <thread>
#include <FFJSON.h>
#include <condition_variable>
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
static atomic<bool> atmcRunning{true};
struct MkHttpArgs {
   FFJSON* ffHttp= nullptr;
   ccp body= nullptr;
   ccp ctype= "text/plain";
   int bsz= -1;
   int code= 200;
   ccp codeMsg= "OK";
   ccp addlHdrs= nullptr;
   bool cchCtrl= false;
   ccp insCntnt= nullptr;
   int insAt= 0;
   MkHttpArgs (
      FFJSON* ffHttp, ccp body, ccp ctype = "text/plain", int bsz=-1,
      const int code = 200, ccp codeMsg = "OK", ccp addlHdrs = "") :
      ffHttp(ffHttp), body(body), ctype(ctype), bsz(bsz), code(code),
      codeMsg(codeMsg), addlHdrs(addlHdrs) {};
   MkHttpArgs () {};
   void init () {
      ffHttp= nullptr;
      body= nullptr;
      ctype= "text/plain";
      bsz= -1;
      int code= 200;
      codeMsg= "OK";
      addlHdrs= nullptr;
      cchCtrl= false;
   }
};

char* fileToStr (fs::path& fspath, char* buf= nullptr);
int mkHttpRes (struct MkHttpArgs& ma);
int mkHttpRes (
   FFJSON& ffHttp, ccp body, ccp ctype = "text/plain", int bsz=-1,
   const int code = 200, ccp codeMsg = "OK", ccp addlHdrs = "");
inline int mkHttpRes (
   FFJSON& ffHttp, string body, ccp ctype = "text/plain",
   const int code = 200, ccp codeMsg = "OK", ccp addlHdrs = "") {
   return mkHttpRes(ffHttp, body.c_str(), ctype, body.length(), code, codeMsg, addlHdrs);
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
#endif
