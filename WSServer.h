/* 
 * File:   WSServer.h
 * Author: Gowtham
 *
 * Created on December 12, 2013, 6:34 PM
 */

#ifndef WSSERVER_H
#define WSSERVER_H

#define MAX_ECHO_PAYLOAD 1024
#define LWS_NO_CLIENT

#include "FerryStream.h"
#include <FFJSON.h>
#include <string>
#include <map>
#include <list>
#include <thread>

using namespace std;
using namespace placeholders;

class WSServer {
public:
   WSServer(
            const char* pcHostName,
            int iDebugLevel=15,
            int iPort=8080,
            int iSecurePort=0,
            const char* pcSSLCertFilePath="",
            const char* pcSSLPrivKeyFilePath="",
            const char* pcSSLCAFilePath="",
            bool bDaemonize=false,
            int iRateUs=0,
            const char* pcInterface="",
            const char* pcClient="",
            int iOpts=0,
            int iSysLogOptions=0
    );
    virtual ~WSServer();
};
#endif /* WSSERVER_H */
