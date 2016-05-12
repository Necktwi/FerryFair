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
#include <libwebsockets.h>

extern std::map<std::string, unsigned int> path_id_map;
extern std::map<int, std::string> id_path_map;
extern std::map<int, std::list<FFJSON*>*> path_packs_map;
extern std::map<FFJSON*, std::string*> pack_string_map;
extern std::map<int, bool> packs_to_send;
extern std::map<lws*, int> wsi_path_map;
extern std::map<int, std::list<lws*>*> path_wsi_map;

extern char* b64_hmt;
extern int b64_hmt_l;
extern uint16_t packBufSize;
extern std::string ferrymedia_store_dir;
extern bool new_pck_chk;
extern int force_exit;
extern char* resource_path;
extern int debug;
extern int stderrfd;
extern int stdoutfd;
extern FFJSON config;
extern std::string hostname;
extern std::string domainname;
extern unsigned int duration;
extern unsigned int starttime;

enum _fp_log_level {
	FPL_MAIN = 1 << 0,
	FPL_FSTREAM_HEART = 1 << 1,
	FPL_WSSERV = 1 << 2,
	FPL_HTTPSERV = 1 << 3,
	FPL_FPORT = 1 << 4,

	NO_NEW_LINE = 1 << 31
};

int init_path(std::string path);
void terminate_path(int path);
void terminate_all_paths();

extern _ff_log_type fs_log_type;
extern unsigned int fs_log_level;


#define fs_notice(level,...) ffl_notice(fs_log_type,fs_log_level,level,__VA_ARGS__)
#define fs_warn(level,...) ffl_warn(fs_log_type,fs_log_level,level,__VA_ARGS__)
#define fs_err(level,...) ffl_err(fs_log_type,fs_log_level,level,__VA_ARGS__)
#define fs_info(level,...) ffl_info(fs_log_type,fs_log_level,level,__VA_ARGS__)

/*
 *  weaker logging can be deselected at configure time using --disable-debug
 *  that gets rid of the overhead of checking while keeping _warn and _err
 *  active
 */
#ifdef _DEBUG

#define fs_debug(level,...) ffl_debug(fs_log_type,fs_log_level,level,__VA_ARGS__)
#define ffl_parser(...) _ff_log(FFL_PARSER, __VA_ARGS__)
#define ffl_header(...)  _ff_log(FFL_HEADER, __VA_ARGS__)
#define ffl_ext(...)  _ff_log(FFL_EXT, __VA_ARGS__)
#define ffl_client(...) _ff_log(FFL_CLIENT, __VA_ARGS__)
#define ffl_latency(...) _ff_log(FFL_LATENCY, __VA_ARGS__)

#endif /* _DEBUG */

#endif	/* GLOBAL_H */