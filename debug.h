/* 
 * File:   debug.h
 * Author: gowtham
 *
 * Created on 7 June, 2013, 4:07 PM
 */

#ifndef DEBUG_H
#define	DEBUG_H
#include "mystdlib.h"
#include <map>
#include <utility>
#include <sys/types.h>

extern int debug;
extern int stdinfd;
extern int stdoutfd;
extern int stderrfd;
extern std::map<pid_t, spawn*> processMap;
extern int child_exit_status;


#endif	/* DEBUG_H */

