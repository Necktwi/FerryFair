#ifndef SMTPCLI
#define SMTPCLI

#include <string>

using namespace std;

int sendMail (
   string host, int port, string mode, string user, string pass, string from,
   string to, string subj, string body
);

#endif //SMTPCLI
