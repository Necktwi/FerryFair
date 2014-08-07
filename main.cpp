/* 
 * File:   main.cpp
 * Author: gowtham
 *
 * Created on 24 November, 2013, 12:25 AM
 */

#define APP_NAME "FairPlay"

#include "ServerSocket.h"
#include "SocketException.h"
#include "mystdlib.h"
#include "myconverters.h"
#include "debug.h"
#include "FFJSON.h"
#include "WSServer.h"
#include "libwebsockets/lib/libwebsockets.h"
#include "FerryStream.hpp"
#include "global.h"
#include "debug.h"
#include <linux/prctl.h>
#include <cstdlib>
#include <string>
#include <iostream>
#include <thread>
#include <exception>
#include <vector>
#include <functional>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <list>
#include <signal.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <getopt.h>

using namespace std;

/*
 * 
 */

int port = 0;
std::map<libwebsocket*, string> wsi_path_map;
std::map < std::string, list < libwebsocket* >*> path_wsi_map;
std::map<string, FerryStream*> ferrystreams;
std::map<libwebsocket*, WSServer::per_session_data__fairplay*> wsi_psdf_map;
std::string configFile;
string recordsFolder = "/var/" + string(APP_NAME) + "records/";
string logFile = "/var/log/" + string(APP_NAME) + ".log";
string initFile = "/etc/init/" + string(APP_NAME) + ".conf";
string initOverrideFile = "/etc/init/" + string(APP_NAME) + ".override";
string initdFile = "/etc/init.d/" + string(APP_NAME);
string installationFolder = "/usr/local/bin/";
string binFile = installationFolder + string(APP_NAME);
string rootBinLnk = "/usr/bin/" + string(APP_NAME);
string srcFolder = "/usr/local/src/" + string(APP_NAME);
string deviceRulesFile = "/etc/udev/rules.d/" + string(APP_NAME) + ".rules";
string runningProcessFile = "/var/tmp/" + string(APP_NAME) + ".pid";
string internetTestURL;
string corpNWGW;
string homeFolder;

int run();

void ferryStreamFuneral(string stream_path) {
    //delete ferrystreams[stream_path];
    ferrystreams[stream_path] = NULL;
}
int next_option;

void print_usage(FILE* stream, int exit_code, char* program_name) {
    fprintf(stream, "Usage: %s <option> [<parameter>]\n", program_name);
    string doc = "-c --configure Configures " + string(APP_NAME) + ""
            "\n-f --config-file <file name> it reads configuration from the "
            "file specified. It should be given ahead of all other options"
            "\n-d --update Updates " + string(APP_NAME) + ""
            "\n-h --help Display this usage information."
            "\n-i --install Installs " + string(APP_NAME) + "."
            "\n-r --reinstall Reinstall the " + string(APP_NAME) + ""
            "\n-s --start=\033[4mTYPE\033[0m Runs client. If \033[4mTYPE\033[0m"
            " is 'daemon' " + string(APP_NAME) + " runs as daemon. If \033["
            "4mTYPE\033[0m is 'normal' " + string(APP_NAME) + " runs normally."
            "\n-u --un-install Un-installs " + string(APP_NAME) + "."
            "\n-x --stop Terminates " + string(APP_NAME) + "."
            "\n---------------------------"
            "\nHave a nice day :)\n\n";
    fprintf(stream, (const char*) doc.c_str());
    exit(exit_code);
}
pid_t rootProcess;
pid_t firstChild;
pid_t secondChild;
pid_t runningProcess;
string runMode = "normal";

void stopRunningProcess() {
    if (runningProcess > 0) {
        ffl_notice(FPOL_MAIN | NO_NEW_LINE, "Stopping current process...");
        if (kill(runningProcess, SIGTERM) != -1) {
            cout << "OK\n";
        } else {
            cout << "FAILED\n";
        }
    }
}

int ferr;

int readConfig() {
    std::ifstream t(configFile);
    std::string str((std::istreambuf_iterator<char>(t)),
            std::istreambuf_iterator<char>());
    try {
        FFJSON config(str);
        homeFolder.assign(config["homeFolder"]);
        port = config["port"];
        internetTestURL.assign(config["internetTestURL"]);
        corpNWGW.assign(config["corpNWGW"]);
        ff_log_type = config["logType"];
        ff_log_level = config["logLevel"];
        return 0;
    } catch (FFJSON::Exception e) {
        ffl_err(FPOL_MAIN, "Reading configuration failed. Please check the configuration file");
        ffl_debug(FPOL_MAIN, "%s", e.what());
        return -1;
    }
    return 0;
};

void secondFork() {
    secondChild = fork();
    if (secondChild != 0) {
        fflush(stdout);
        int status;
wait_till_child_dead:
        int deadpid = waitpid(secondChild, &status, 0);
        if (deadpid == -1 && waitpid(secondChild, &status, WNOHANG) == 0) {
            goto wait_till_child_dead;
        }
        ffl_warn(FPOL_MAIN, "%d process exited!", deadpid);
        secondFork();
    } else {
        secondChild = getpid();
        ffl_notice(FPOL_MAIN, "second child started; pid= %d", secondChild);
        prctl(PR_SET_PDEATHSIG, SIGHUP);
        run();
    }
}

void firstFork() {
    readConfig();
    if (runMode.compare("daemon") == 0) {
        if ((debug & 1) == 1) {
            dup2(ferr, 1);
            stdoutfd = ferr;
        } else {
            close(1);
        }
    }
    firstChild = fork();
    if (firstChild != 0) {
        int status;
wait_till_child_dead:
        int deadpid = waitpid(firstChild, &status, 0);
        if (deadpid == -1 && waitpid(firstChild, &status, WNOHANG) == 0) {
            goto wait_till_child_dead;
        }
        ffl_debug(FPOL_MAIN, "%d process exited", deadpid);
        firstFork();
    } else {
        firstChild = getpid();
        ffl_notice(FPOL_MAIN, "firstChild started; pid=%d", firstChild);
        fflush(stdout);
        prctl(PR_SET_PDEATHSIG, SIGHUP);
        secondFork();
    }
}

int install() {

}

int reinstall() {

}

int uninstall() {

}

int update() {

}

string readConfigValue(string name) {
    std::ifstream t(configFile);
    std::string str((std::istreambuf_iterator<char>(t)),
            std::istreambuf_iterator<char>());
    std::string ret;
    try {
        FFJSON config(str);
        ret.assign((const char*) config[name]);
        return ret;
    } catch (FFJSON::Exception e) {
        return "";
    }
}

void writeConfigValue(string name, string value) {

}

void configure() {
    if (readConfig() == -1)return;
    cout << "Current " << APP_NAME << " configuration:\n"
            "----------------------------\n";
    cout << "bootup:\t" << readConfigValue("bootup") << "\n";
    cout << "internetTestURL:\t" << internetTestURL << "\n";
    cout << "corporateNetworkGateway:\t" << corpNWGW << "\n";
    cout << "debug:\t" << string(itoa(debug)) << "\n";
    cout << "----------------------------\n"
            "Set or Add configuration property\n";
    string pn;
    string val;
    while (true) {
        cout << "\nProperty name:";
        pn = inputText();
        if (pn.length() != 0) {
            cout << "\nValue:";
            val = inputText();
            if (pn.compare("bootup") == 0) {
                if (geteuid() != 0) {
                    cout << "\nPlease login as root are sudo user.\n";
                } else {
                    struct stat st;
                    if (val.compare("true") == 0) {
                        if (stat(initFile.c_str(), &st) != -1) {
                            cout << "\nAllowing to run " + string(APP_NAME) + " at startup...";
                            if (stat(initOverrideFile.c_str(), &st) != -1) {
                                string rmfcmd = "rm " + initOverrideFile;
                                system(rmfcmd.c_str());
                            }
                            writeConfigValue(pn, val);
                            cout << "ok\n";
                        } else {
                            cout << "\nInstall " + string(APP_NAME) + " in first.\n";
                        }
                    } else if (val.compare("false") == 0) {
                        if (stat(initFile.c_str(), &st) != 1) {
                            cout << "\nDisabling " + string(APP_NAME) + " to run at startup...";
                            if (stat(initOverrideFile.c_str(), &st) == -1) {
                                int fd = open(initOverrideFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
                                string buf = "manual";
                                write(fd, buf.c_str(), buf.length());
                                close(fd);
                            }
                            writeConfigValue(pn, val);
                            cout << "ok\n";
                        } else {
                            cout << "\nInstall " + string(APP_NAME) + " in first.\n";
                        }
                    }
                }
            } else {
                writeConfigValue(pn, val);
            }
        } else {
            cout << "\n";
            fflush(stdout);
            break;
        }
    }
}

int test(void *) {

}

int run() {
    port = 92711;
    ServerSocket* ss;
    debug = 1;

    WSServer::WSServerArgs ws_server_args;
    memset(&ws_server_args, 0, sizeof (WSServer::WSServerArgs));
    ws_server_args.path_wsi_map = &path_wsi_map;
    ws_server_args.wsi_path_map = &wsi_path_map;
    ws_server_args.debug_level = 31;
    ws_server_args.ferryStreams = &ferrystreams;
    WSServer* wss = new WSServer(&ws_server_args);
    try {
        ss = new ServerSocket(92711);
    } catch (SocketException e) {
        ffl_err(FPL_MAIN, "Unable to create socket on port: %d", port);
        exit(1);
    }
    while (true && !force_exit) {
        try {
            ffl_notice(FPL_MAIN, "waiting for a connection on %d ...", 92711);
            FerryStream* fs = new FerryStream(ss->accept(),
                    &ferryStreamFuneral);
            if (ferrystreams[fs->path] == NULL) {
                ferrystreams[fs->path] = fs;
            } else {
                if (ferrystreams[fs->path]->isConnectionAlive()) {
                    delete fs;
                } else {
                    delete ferrystreams[fs->path];
                    ferrystreams[fs->path] = fs;
                }
            }
            ffl_notice(FPL_MAIN, "a connection accepted.");
        } catch (SocketException e) {
            ffl_warn(FPL_MAIN, "Exception accepting incoming connection: %s",
                    e.description().c_str());
        } catch (FerryStream::Exception e) {
            ffl_err(FPL_MAIN, "Exception creating a new FerryStream: %s",
                    e.what());
            fflush(stdout);
        }
    }
    delete ss;
    return 0;
}

int main(int argc, char** argv) {
    const char* const short_options = "cdf:hirs:tux";
    string opt;
    const struct option long_options[] = {
        {"configure", 0, NULL, 'c'},
        {"config-file", 1, NULL, 'f'},
        {"update", 0, NULL, 'd'},
        {"help", 0, NULL, 'h'},
        {"install", 0, NULL, 'i'},
        {"reinstall", 0, NULL, 'r'},
        {"start", 1, NULL, 's'},
        {"test", 0, NULL, 't'},
        {"uninstall", 0, NULL, 'u'},
        {"stop", 0, NULL, 'x'},
        {NULL, 0, NULL, 0}
    };
    struct stat st;
    configFile = "/etc/" + std::string(APP_NAME) + ".conf";
    if (stat(configFile.c_str(), &st) == -1) {
        configFile = "config.json";
    }
    do {
        next_option = getopt_long(argc, argv, short_options, long_options,
                NULL);
        switch (next_option) {
            case 'h':
                print_usage(stdout, 0, argv[0]);
                break;
            case 's':
                stopRunningProcess();
                rootProcess = getpid();
                opt = string(optarg);
                if (opt.compare("daemon") == 0) {
                    runMode = opt;
                    close(1);
                    dup2(ferr, 2);
                    stderrfd = ferr;
                    firstFork();
                } else if (opt.compare("normal") == 0) {
                    run();
                }
                break;
            case 'i':
                stopRunningProcess();
                install();
                break;
            case 'r':
                reinstall();
                break;
            case 'c':
                configure();
                break;
            case 'u':
                uninstall();
                break;
            case 'x':
                stopRunningProcess();
                break;
            case 'd':
                update();
                break;
            case 't':
                test(NULL);
                break;
            case 'f':
                configFile = string(optarg);
                break;
            case '?':
                print_usage(stderr, 1, argv[0]);
                break;
            case -1:
                print_usage(stderr, 1, argv[0]);
                break;
        }
    } while (next_option != -1);

    return 0;
}

