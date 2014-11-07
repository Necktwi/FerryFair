#include "global.h"
#include <base/mystdlib.h>
#include <base/logger.h>
#include <string>
#include <list>
#include <libwebsockets.h>
#include <base/FFJSON.h>
#include <mutex>

std::map<std::string, unsigned int> path_id_map;
std::map<int, std::string> id_path_map;
std::map<int, std::list<FFJSON*>*> path_packs_map;
std::map<FFJSON*, std::string*> pack_string_map;
std::map<int, bool> packs_to_send;
std::map<libwebsocket*, int> wsi_path_map;
std::map<int, std::list<libwebsocket*>*> path_wsi_map;

char* b64_hmt = NULL;
int b64_hmt_l = 0;
bool new_pck_chk = false;
uint16_t packBufSize = 60;
std::string ferrymedia_store_dir = "/var/ferrymedia_store/";
int force_exit = 0;
int debug = 0;
int stderrfd = -1;
int stdoutfd = -1;
int init_path_id = 0;
std::mutex ipMutex;

int init_path(std::string path) {
	ipMutex.lock();
	if (path_id_map.find(path) == path_id_map.end()) {
		path_id_map[path] = ++init_path_id;
		id_path_map[init_path_id] = path;
		path_packs_map[init_path_id] = new std::list<FFJSON*>();
		path_wsi_map[init_path_id] = new std::list<libwebsocket*>();
	}
	ipMutex.unlock();
	return path_id_map[path];
}

void terminate_path(int path) {
	ipMutex.lock();
	std::map<int, std::string>::iterator i = id_path_map.find(path);
	if (i != id_path_map.end()) {
		std::map<int, std::list<FFJSON*>*>::iterator j =
				path_packs_map.find(i->first);
		std::list<FFJSON*>::iterator k = j->second->begin();
		while (k != j->second->end()) {
			std::map<FFJSON*, std::string*>::iterator l =
					pack_string_map.find(*k);
			delete l->second;
			pack_string_map.erase(l);
			delete *k;
			k++;
		}
		delete j->second;
		delete path_wsi_map[i->first];
		path_wsi_map.erase(i->first);
		path_packs_map.erase(j);
		path_id_map.erase(i->second);
		id_path_map.erase(i);
	}
	ipMutex.unlock();
}

void terminate_all_paths() {
	ipMutex.lock();
	std::map<int, std::string>::iterator i = id_path_map.begin();
	while (i != id_path_map.end()) {
		std::map<int, std::string>::iterator l = i;
		std::map<int, std::list<FFJSON*>*>::iterator j = path_packs_map.find(i->first);
		std::list<FFJSON*>::iterator k = j->second->begin();
		while (k != j->second->end()) {
			std::map<FFJSON*, std::string*>::iterator l = pack_string_map.find(*k);
			delete l->second;
			pack_string_map.erase(l);
			delete *k;
			k++;
		}
		delete j->second;
		delete path_wsi_map[i->first];
		path_packs_map.erase(j);
		path_wsi_map.erase(i->first);
		i++;
		path_id_map.erase(l->second);
		id_path_map.erase(l);
	}
	ipMutex.unlock();
}

/**
 * logging section
 */


