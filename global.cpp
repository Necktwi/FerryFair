#include "global.h"
#include "mystdlib.h"
#include "logger.h"
#include <string>
#include <list>

std::string ferrymedia_store_dir = "/var/ferrymedia_store/";
std::map<std::string, bool> packs_to_send;
std::map<std::string, std::string*> streamHeads;
bool new_pck_chk = false;
int force_exit = 0;
char resource_path[50] = "/home/gowtham/FairPlay/";

/**
 * logging section
 */


/**
 * enabled log level
 */

int ff_log_type = FFL_ERR | FFL_WARN | FFL_NOTICE;
unsigned int ff_log_level = FPL_FPORT | FPL_WSSERV | FPL_HTTPSERV | FPL_MAIN | FPL_FSTREAM_HEART;