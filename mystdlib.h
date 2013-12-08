/* 
 * File:   mystdlib.h
 * Author: newmek7
 *
 * Created on 26 March, 2013, 10:25 AM
 */

#ifndef MYSTDLIB_H
#define	MYSTDLIB_H
#include <sys/types.h>
#include <string>
#include <signal.h>
#include <list>

void initTermios(int echo);
void resetTermios(void);
char getch_(int echo);
char getch(void);
char getche(void);
int copyfile(std::string src, std::string dst);
std::string getCurrentDir();
std::string getMachineName();
int rmmydir(std::string dirn);
std::string inputPass();
std::string inputText();
std::string getStdoutFromCommand(std::string cmd);
std::string getTime();
std::string get_command_line(pid_t pid);
int poke(std::string ip);
int getIp();
std::string GetPrimaryIp();
std::string get_fd_contents(int fd);
char const * sperm(__mode_t mode);

/*Corrected on system time change*/
class FerryTimeStamp {
public:
    time_t t;
    FerryTimeStamp();
    ~FerryTimeStamp();
    static std::list<time_t*> ferryTimesList;
};

class spawn {
private:
    int cpstdinp[2];
    int cpstdoutp[2];
    int cpstderrp[2];

public:
    static bool processCleaned;
    static void defaultOnStopHandler(spawn* process);
    pid_t cpid = 0;
    int cpstdin = -1;
    int cpstdout = -1;
    int cpstderr = -1;
    int childExitStatus = 0;
    std::string cmd = "";
    std::string cmdName = "";
    void (*onStopHandler)(spawn*);

    spawn();
    spawn(std::string command, bool daemon = false, void (*onStopHandler)(spawn*) = NULL, bool freeChild = false, bool block = false);
    int getChildExitStatus();
    int pkill(int signal = SIGTERM);
};
#endif	/* MYSTDLIB_H */

