#ifndef FERFAR
#define FERFAR

#include <FFJSON.h>
#include <string>
#include <mutex>

using namespace std;

int ferryfair (FFJSON& ffHttp);
void initFerryFair (FFJSON& cfg);
void uninitFerryFair ();
void saveFerryFair (void* pcfg);
extern mutex mtxSaveUsers;
extern mutex mtxSaveRbs;
extern atomic<bool> saveUsers;
extern atomic<bool> saveRbs;
extern atomic<bool> saveNameints;
extern atomic<bool> saveFerryfair;
#endif
