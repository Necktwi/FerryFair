#ifndef HTTPS
#define HTTPS

#include <functional>
#include <queue>
#include <thread>
#include <FFJSON.h>
#include <condition_variable>

extern FFJSON cfg;
enum HTTPLOG {
   HL = 1<<11,
   FL = 1<<12,
   SL = 1<<13,
   SLL = 1<<14,
   SM = 1<<15,
   FLL = 1<<16,
   HSL = 1<<17
};
static atomic<bool> g_running{true};
struct MkHttpArgs {
   FFJSON* ffHttp = nullptr;
   ccp body = nullptr;
   ccp ctype = "text/plain";
   int bsz=-1;
   const int code = 200;
   ccp codeMsg = "OK";
   ccp addlHdrs = "";
   bool cchCtrl = false;
   MkHttpArgs (
      FFJSON* ffHttp, ccp body, ccp ctype = "text/plain", int bsz=-1,
      const int code = 200, ccp codeMsg = "OK", ccp addlHdrs = "") :
      ffHttp(ffHttp), body(body), ctype(ctype), bsz(bsz), code(code),
      codeMsg(codeMsg), addlHdrs(addlHdrs) {};
   MkHttpArgs () {};
};
string mkHttpRes (struct MkHttpArgs& ma);
string mkHttpRes (
   FFJSON& ffHttp, ccp body, ccp ctype = "text/plain", int bsz=-1,
   const int code = 200, ccp codeMsg = "OK", ccp addlHdrs = "");
inline string mkHttpRes (
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
   void enqueue(function<void()> job);
   void join ();

private:
   vector<thread> workers_;
   queue<function<void()>> jobs_;
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
constexpr uint32_t operator "" _hash (const char *s, size_t) {
   return fnv1a(s);
}
#endif
