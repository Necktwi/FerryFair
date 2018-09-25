/* Gowtham Kudupudi 24/11/2013
 * ©FerryFair
 * */                                                                          
#include "global.h"
#include "config.h"
#include "WSServer.h"
#include "FerryStream.h"
#include "Authentication.h"
#include <ferrybase/ServerSocket.h>
#include <ferrybase/SocketException.h>
#include <ferrybase/mystdlib.h>
#include <ferrybase/myconverters.h>
#include <FFJSON.h>
#include <logger.h>
#include <libwebsockets.h>
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

/* enabled log level
 * */

 //int ff_log_type = FFL_ERR | FFL_WARN | FFL_NOTICE | FFL_DEBUG;
 //unsigned int ff_log_level = FPL_FPORT | FPL_WSSERV | FPL_HTTPSERV | FPL_MAIN
 //		| FPL_FSTREAM_HEART;
 //unsigned int ff_log_level = FPL_HTTPSERV;


int child_exit_status = 0;
int port = 0;
std::map<lws*, WSServer::per_session_data__fairplay*> wsi_psdf_map;
std::string configFile;
string recordsFolder = "/var/" APP_NAME "records/";
string logFile = "/var/log/" APP_NAME ".log";
string initFile = "/etc/init/" APP_NAME ".conf";
string initOverrideFile = "/etc/init/" APP_NAME ".override";
string initdFile = "/etc/init.d/" APP_NAME;
string installationFolder = "/usr/local/bin/";
string binFile = installationFolder + APP_NAME;
string rootBinLnk = "/usr/bin/" APP_NAME;
string srcFolder = "/usr/local/src/" APP_NAME;
string deviceRulesFile = "/etc/udev/rules.d/" APP_NAME ".rules";
string runningProcessFile = "/var/tmp/" APP_NAME ".pid";
string internetTestURL;
string corpNWGW;
string homeFolder;
FFJSON config;
string hostname;
string domainname;
unsigned int duration = 0;
int ferr = 0;
int run ();

void groomLogFile () {
   if (ferr > 0)
      close(ferr);
   struct stat statbuf;
   int stat_r = stat(logFile.c_str(), &statbuf);
   ferr = open (
      logFile.c_str(), O_WRONLY | (stat_r == -1 || statbuf.st_size > 5000000) ?
      (O_CREAT | O_TRUNC) : O_APPEND, 0600
   );
}

void ferryStreamFuneral (int stream_path) {}
int next_option;

void print_usage(FILE* stream, int exit_code, char* program_name) {
   fprintf(stream, "Usage: %s <option> [<parameter>]\n", program_name);
   string doc = "-c --configure Configures " APP_NAME ""
      "\n-f --config-file <file name> it reads configuration from the "
      "file specified. It should be given ahead of all other options"
      "\n-d --update Updates " APP_NAME ""
      "\n-h --help Display this usage information."
      "\n-i --install Installs " APP_NAME "."
      "\n-r --reinstall Reinstall the " APP_NAME ""
      "\n-s --start=\033[4mTYPE\033[0m Runs client. If \033[4mTYPE\033[0m"
      " is 'daemon' " APP_NAME " runs as daemon. If \033["
      "4mTYPE\033[0m is 'normal' " APP_NAME " runs normally."
      "\n-t --time <seconds> runs the program of duration <seconds>"
      "\n-u --un-install Un-installs " APP_NAME "."
      "\n-x --stop Terminates " APP_NAME "."
      "\n---------------------------"
      "\nHave a nice day :)\n\n";
   fputs((const char*)doc.c_str(), stream);
}
pid_t rootProcess;
pid_t firstChild;
pid_t secondChild;
pid_t runningProcess;
string runMode = "normal";

void stopRunningProcess() {
   if (runningProcess > 0) {
      ffl_notice(FPL_MAIN | NO_NEW_LINE, "Stopping current process...");
      if (kill(runningProcess, SIGTERM) != -1) {
         cout << "OK\n";
      }
      else {
         cout << "FAILED\n";
      }
   }
}

int readConfig() {
   ffl_notice(FPL_MAIN, "ConfigFile: %s", configFile.c_str());
   std::ifstream t(configFile);
   std::string str((std::istreambuf_iterator<char>(t)),
      std::istreambuf_iterator<char>());
   try {
      config.init(str);
      homeFolder.assign((const char*)config["homeFolder"]);
      port = config["port"];
      internetTestURL.assign((const char*)config["internetTestURL"]);
      corpNWGW.assign((const char*)config["corpNWGW"]);
      unsigned int ff_log_type = 0;
      unsigned int ff_log_level = 0;
      if (config["logType"].isType(FFJSON::ARRAY)) {
         for (FFJSON::Iterator it = config["logType"].begin();
            it != config["logType"].end(); it++)
            ff_log_type |= (unsigned int)*it;
      }
      else {
         ff_log_type = config["logType"];
      }
      if (config["logLevel"].isType(FFJSON::ARRAY)) {
         for (FFJSON::Iterator it = config["logLevel"].begin();
            it != config["logLevel"].end(); it++)
            ff_log_level |= (unsigned int)*it;
      }
      else {
         ff_log_level = config["logLevel"];
      }
      fflAllowedType = (FF_LOG_TYPE)ff_log_type;
      fflAllowedLevel = ff_log_level;
      ffl_debug(FPL_MAIN, "fflAllowedType: %08X, fflAllowedLevel: %08X", (unsigned int)fflAllowedType, (unsigned int)fflAllowedLevel);
   }
   catch (FFJSON::Exception e) {
      ffl_err(FPL_MAIN, "Reading configuration failed. Please check the configuration file");
      ffl_debug(FPL_MAIN, "%s", e.what());
      return -1;
   }
   std::ifstream hfile ("/etc/hostname", ios::ate | ios::in);
   if (!hfile .is_open()) {
      ffl_err (0, "/etc/hostname not found.");
      exit (-1);
   }   
   hfile. seekg(ios::end);
   string hostn;
   hostn .reserve (hfile .tellg ());
   hfile .seekg (ios::beg);
   hostn .assign ((std ::istreambuf_iterator<char> (hfile)),
                  std::istreambuf_iterator<char>());
   FFJSON::trimWhites(hostn);
   int dnail = hostn.find('.');
   if (dnail != string::npos) {
      hostname = hostn.substr(0, dnail);
      domainname = hostn.substr(dnail + 1);
   }
   else {
      hostname = hostn;
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
      ffl_warn(FPL_MAIN, "%d process exited!", deadpid);
      secondFork();
   }
   else {
      secondChild = getpid();
      ffl_notice(FPL_MAIN, "second child started; pid= %d", secondChild);
      prctl(PR_SET_PDEATHSIG, SIGHUP);
      run();
   }
}

void firstFork() {
   if (runMode.compare("daemon") == 0) {
      if ((debug & 1) == 1) {
         dup2(ferr, 1);
         stdoutfd = ferr;
      }
      else {
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
      ffl_debug(FPL_MAIN, "%d process exited", deadpid);
      firstFork();
   }
   else {
      firstChild = getpid();
      ffl_notice(FPL_MAIN, "firstChild started; pid=%d", firstChild);
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
      ret.assign((const char*)config[name]);
      return ret;
   }
   catch (FFJSON::Exception e) {
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
            }
            else {
               struct stat st;
               if (val.compare("true") == 0) {
                  if (stat(initFile.c_str(), &st) != -1) {
                     cout << "\nAllowing to run " APP_NAME " at startup...";
                     if (stat(initOverrideFile.c_str(), &st) != -1) {
                        string rmfcmd = "rm " + initOverrideFile;
                        system(rmfcmd.c_str());
                     }
                     writeConfigValue(pn, val);
                     cout << "ok\n";
                  }
                  else {
                     cout << "\nInstall " APP_NAME " in first.\n";
                  }
               }
               else if (val.compare("false") == 0) {
                  if (stat(initFile.c_str(), &st) != 1) {
                     cout << "\nDisabling " APP_NAME " to run at startup...";
                     if (stat(initOverrideFile.c_str(), &st) == -1) {
                        int fd = open(initOverrideFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0755);
                        string buf = "manual";
                        write(fd, buf.c_str(), buf.length());
                        close(fd);
                     }
                     writeConfigValue(pn, val);
                     cout << "ok\n";
                  }
                  else {
                     cout << "\nInstall " APP_NAME " in first.\n";
                  }
               }
            }
         }
         else {
            writeConfigValue(pn, val);
         }
      }
      else {
         cout << "\n";
         fflush(stdout);
         break;
      }
   }
}

unsigned int starttime = time(NULL);

int run () {
   groomLogFile();
   if (runMode.compare("daemon") == 0) {
      dup2(ferr, 2);
      dup2(2, 1);
   }
   port = config["port"];
   ServerSocket* ss = NULL;
   debug = 1;
   b64_hmt = base64_encode((const unsigned char*)JPEGImage::StdHuffmanTable, 420, (size_t*)& b64_hmt_l);
   WSServer* wss = new WSServer(
      config["hostName"],
      config["lwsDebug"],
      config["HTTPPort"],
      config["HTTPSPort"],
      config["sslCert"],
      config["sslKey"],
      config["sslCA"]
   );
   try {
      ss = new ServerSocket(port);
   }
   catch (SocketException e) {
      ffl_err(FPL_MAIN, "Unable to create socket on port: %d", port);
   }
   while (ss && !force_exit && (duration == 0 || duration > (time(NULL) - starttime))) {
      try {
         ffl_notice(FPL_MAIN, "waiting for a connection on %d ...", port);
         FerryStream* fs = new FerryStream(ss->accept(),
            &ferryStreamFuneral);
         cleanDeadFSList();
         ffl_notice(FPL_MAIN, "a connection accepted.");
      }
      catch (SocketException e) {
         ffl_warn(FPL_MAIN, "Exception accepting incoming connection: %s",
            e.description().c_str());
      }
      catch (FerryStream::Exception e) {
         ffl_err(FPL_MAIN, "Exception creating a new FerryStream: %s",
            e.what());
      }
   }
   force_exit = 1;
   sleep(5);
   cleanDeadFSList();
   cleanLiveFSList();
   delete ss;
   delete wss;
   terminate_all_paths();
   free(b64_hmt);
   ffl_notice(FPL_MAIN, "BYE!");
   return 0;
}

int main (int argc, char** argv) {
   Authentication_ Authentication ("gowtham", "tornshoees");
   if (Authentication .is_valid()) {
      cout << "Authorized" << endl;
   } else {
      cout << "Unauthorized" << endl;
   }
   groomLogFile ();
   stdinfd = dup (0);
   stdoutfd = dup (1);
   stderrfd = dup (2);
   const char* const short_options = "cdf:hirs:t:ux";
   string opt;
   const struct option long_options [] = {
      {"configure", 0, NULL, 'c'},
      {"config-file", 1, NULL, 'f'},
      {"update", 0, NULL, 'd'},
      {"help", 0, NULL, 'h'},
      {"install", 0, NULL, 'i'},
      {"reinstall", 0, NULL, 'r'},
      {"start", 1, NULL, 's'},
      {"time", 1, NULL, 't'},
      {"uninstall", 0, NULL, 'u'},
      {"stop", 0, NULL, 'x'},
      {NULL, 0, NULL, 0}
   };
   struct stat st;
   configFile = "config.json";
   if (stat (configFile.c_str(), &st) == -1) {
      configFile = string (CONFIG_INSTALL_DIR) + "/" + string (APP_NAME) +
         ".json";
   }
   do {
      next_option = getopt_long(argc, argv, short_options, long_options,
         NULL);
      switch (next_option) {
      case 'h':
         print_usage(stdout, 0, argv[0]);
         break;
      case 's':
         readConfig();
         stopRunningProcess();
         rootProcess = getpid();
         opt = string(optarg);
         if (opt.compare("daemon") == 0) {
            runMode = opt;
            close(1);
            dup2(ferr, 2);
            stderrfd = ferr;
            firstFork();
         }
         else if (opt.compare("normal") == 0) {
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
         duration = std::stoi(optarg);
         break;
      case 'f':
         configFile = string(optarg);
         break;
      case '?':
         print_usage(stderr, 1, argv[0]);
         break;
      case -1:
         break;
      }
   } while (next_option != -1);

   return 0;
}

