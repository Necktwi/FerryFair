#ifndef FERFAR
#define FERFAR

#include <FFJSON.h>
#include <string>

using namespace std;

string ferryfair (FFJSON& ffHttp, FFJSON& vhost);
void initFerryFair (FFJSON& cfg);

#endif
