#include "debug.h"
#include <sys/types.h>
#include "mystdlib.h"

int debug = 0;
int stdinfd;
int stdoutfd;
int stderrfd;
std::map<pid_t, spawn*> processMap;
int child_exit_status;
