#ifndef HTTPS
#define HTTPS

#include <functional>
#include <queue>
#include <thread>
#include <FFJSON.h>

extern FFJSON cfg;
enum HTTPLOG {
   HL = 1<<11
};
static atomic<bool> g_running{true};
string mkHttpRes (FFJSON& ffHttp, const string& body,
                  const string &ctype = "text/plain",
                  const int code = 200,
                  const string &codeMsg = "OK",
                  const string &addlHdrs = "");
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
   
   void start (size_t n);

   void stop ();
};
extern ThreadPool pool;
#endif
