#ifndef HTTPS
#define HTTPS

#include <FFJSON.h>

extern FFJSON cfg;
enum HTTPLOG {
   HL = 1<<11
};
static atomic<bool> g_running{true};
string mkHttpRes (const string &body,
                  const string &ctype = "text/plain",
                  const int code = 200,
                  const string &codeMsg = "OK",
                  const string &addlHdrs = "");
#endif
