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
#include "mongoose.h"
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
private:
/*
   void fn (struct mg_connection *c, int ev, void *ev_data, void *fn_data);
   void fn_tls (struct mg_connection *c, int ev, void *ev_data, void *fn_data);
   void tls_ntls_common (struct mg_connection* c, int ev, void* ev_data,
                         void* fn_data);
   void mailfn (struct mg_connection *c, int ev, void *ev_data, void *fn_data);
   struct mg_mgr mgr, mail_mgr;

   thread_local static WSServer* toHttpListen;
   static void gfn (struct mg_connection *c, int ev, void *ev_data,
                    void *fn_data);
   static void gfn_tls (struct mg_connection *c, int ev, void *ev_data,
                        void *fn_data);
   static void gmailfn (struct mg_connection *c, int ev, void *ev_data,
                        void *fn_data);
   const char* server = "tcp://ferryfair.com:25";
   const char* user = "Necktwi";
   const char* pass = "tornshoes";
   char* to = "gowtham.kudupudi@gmail.com";

   const char* from = "FerryFair";
   const char* subj = "Test email from Mongoose library!";
   const char* mesg = "Hi!\nThis is a test message.\nBye.";

   bool s_quit = false;
*/
};
#endif /* WSSERVER_H */
