/*
 * Gowtham Kudupudi 2018/07/26
 * ©FerryFair
 */

//#if defined(LWS_USE_POLARSSL)
//#else
//#if defined(LWS_USE_MBEDTLS)
//#else
//#if defined(LWS_OPENSSL_SUPPORT) && defined(LWS_HAVE_SSL_CTX_set1_param)
///* location of the certificate revocation list */
//extern char crl_path[1024];
//#endif
//#endif
//#endif

#include "global.h"
#include "WSServer.h"
#include "mongoose.h"
#include "FerryStream.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>
#ifdef WIN32
#else
#include <syslog.h>
#endif
#include <signal.h>
#include <map>
#include <vector>
#include <iostream>
#include <string>
#include <sys/stat.h>


#include <ferrybase/JPEGImage.h>
#include <FFJSON.h>
#include <logger.h>
#include <ferrybase/mystdlib.h>
#include <ferrybase/Socket.h>
#include <iostream>
#include <malloc.h>
#include <functional>

using namespace std;

static void parseHTTPHeader (const char* uri, size_t len, FFJSON& sessionData) 
{
   unsigned int i=0;
   unsigned int pairStartPin=i;
   while(uri[i]!='\0'){
      if(uri[i]=='\n'){
         if(uri[i-1]=='\n' || (uri[i-1]=='\r' && uri[i-2]=='\n'))return;
         
         int k=i;
         if(uri[i-1]=='\r')k=i-1;
         if(pairStartPin==0){
            int j=pairStartPin;
            while(uri[j]!=' ' && j<k){
               ++j;
            }
            if(j==k) goto line_done;
            sessionData["path"]=string(uri,j);
            ++j;
            sessionData["version"]=string(uri+j,k-j);            
         }else{
            int j=pairStartPin;
            while(uri[pairStartPin]==' ')++pairStartPin;
            while(uri[j]!=':' && j<k)++j;
            if(j==k) goto line_done;
            int keySize=j-pairStartPin;
            ++j;
            ++j;
            sessionData[string(uri+pairStartPin,keySize)]=string(uri+j,k-j);
         }
line_done:         
         pairStartPin=i+1;
      }
      ++i;
   }
}

string get_subdomain(string host) {
   return host.substr(0, host.find((const char*)config["hostname"]));
}

static void tls_ntls_common(struct mg_connection* c, int ev, void* ev_data,
                            void* fn_data)
{
   struct mg_http_serve_opts opts = {
      .root_dir = "/home/Necktwi/workspace/WWW"
   };   // Serve local dir
   if (ev == MG_EV_HTTP_MSG) {
      unsigned char b[4];
      b[0] = c->rem.ip & 0xFF;
      b[1] = (c->rem.ip >> 8) & 0xFF;
      b[2] = (c->rem.ip >> 16) & 0xFF;
      b[3] = (c->rem.ip >> 24) & 0xFF;
      printf("Remote IP: %d.%d.%d.%d\n", b[0], b[1], b[2], b[3]);
      struct mg_http_message* hm = (struct mg_http_message*) ev_data;
      printf("hm->uri:\n%s\n", hm->uri);

      FFJSON sessionData;
      parseHTTPHeader((const char*)hm->uri.ptr, hm->uri.len, sessionData);
      //printf("%s\n",(char*)sessionData["Referer"]);
      if (sessionData["Host"]==nullptr) {
         return;
      } else if (
         !strcmp(sessionData["Host"],"www.ferryfair.com") ||
         !strcmp(sessionData["Host"], "127.0.0.2")
      ) {
         printf("Development zone.\n");
         opts.root_dir="/home/Necktwi/workspace/WWW-development";
      } else if (config["virtualWebHosts"][get_subdomain(sessionData["Host"])]){
         opts.root_dir = config["virtualWebHosts"][get_subdomain(sessionData["Host"])];
         
      }
      
      if (!strcmp(sessionData["path"], "/login")) {
         printf("login\n");
         FFJSON body(string(hm->body.ptr, hm->body.len));
         printf("User: %s\n", (const char*)body["username"]);
         printf("Pass: %s\n", (const char*)body["password"]);
         const char *headers = "content-type: text/json\r\n";
         // Return data, up to CHUNK_SIZE elements
         mg_http_reply(c, 200, headers, "{\"%s\":%s}",
                       "login","true");
      } else {   
         mg_http_serve_dir(c, (mg_http_message*)ev_data, &opts);
      }
      
      if (valgrind_test)
         force_exit=true;
   }
}
static void fn(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
   tls_ntls_common(c, ev, ev_data, fn_data);
}
static void fn_tls(struct mg_connection *c, int ev, void *ev_data,
                   void *fn_data
) {
   if (ev == MG_EV_ACCEPT) {
      struct mg_tls_opts opts = {
         .cert = "/etc/letsencrypt/live/ferryfair.com/cert.pem",
         .certkey = "/etc/letsencrypt/live/ferryfair.com/privkey.pem"
      };
      mg_tls_init(c, &opts);
   }
   tls_ntls_common(c, ev, ev_data, fn_data);
}
WSServer::WSServer (
   const char* pcHostName, int iDebugLevel,
   int iPort, int iSecurePort, const char* pcSSLCertFilePath,
   const char* pcSSLPrivKeyFilePath, const char* pcSSLCAFilePath,
   bool bDaemonize, int iRateUs, const char* pcInterface,
   const char* pcClient, int iOpts,
   #ifndef LWS_NO_CLIENT
      const char* pcAddress, unsigned int uiOldus,
   #ifdef LIBWEBSOCKETS
   struct lws* pWSI,
   #endif
   #endif
   int iSysLogOptions
) 
{
   struct mg_mgr mgr;
   mg_mgr_init(&mgr);
   mg_http_listen(&mgr, "0.0.0.0:443", fn_tls, NULL);
   mg_http_listen(&mgr, "0.0.0.0:80", fn, NULL);
   while (!force_exit) mg_mgr_poll(&mgr, 1000);
}
WSServer::~WSServer () {
   
}
//string WSServer::per_session_data__http::vhost();
