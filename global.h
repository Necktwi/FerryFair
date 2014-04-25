/* 
 * File:   global.h
 * Author: Gowtham
 *
 * Created on December 26, 2013, 12:16 PM
 */

#ifndef GLOBAL_H
#define	GLOBAL_H

#include "FFJSON.h"
#include "libwebsockets/lib/libwebsockets.h"
#include <string>
#include <map>
#include <list>

extern std::string ferrymedia_store_dir;
extern std::map<std::string, bool> packs_to_send;
extern bool new_pck_chk;
extern int force_exit;

#endif	/* GLOBAL_H */