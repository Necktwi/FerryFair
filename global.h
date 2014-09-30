/* 
 * File:   global.h
 * Author: Gowtham
 *
 * Created on December 26, 2013, 12:16 PM
 */

#ifndef GLOBAL_H
#define	GLOBAL_H

#define NELEMS(x)  (sizeof(x) / sizeof(x[0]))

#include <base/FFJSON.h>
#include <string>
#include <map>
#include <list>

extern std::string ferrymedia_store_dir;
extern std::map<std::string, bool> packs_to_send;
extern std::map<std::string, std::string*> streamHeads;
extern bool new_pck_chk;
extern int force_exit;
extern char* resource_path;
extern int debug;
extern int stderrfd;
extern int stdoutfd;

enum _fp_log_level {
    FPL_MAIN = 1 << 0,
    FPL_FSTREAM_HEART = 1 << 1,
    FPL_WSSERV = 1 << 2,
    FPL_HTTPSERV = 1 << 3,
    FPL_FPORT = 1 << 4,

    NO_NEW_LINE = 1 << 31
};
#endif	/* GLOBAL_H */