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
#include <ferrybase/myconverters.h>
#include <iostream>
#include <malloc.h>
#include <functional>

typedef const char* ccp;
using namespace std;
using namespace std::placeholders;

enum { EHLO, STARTTLS, STARTTLS_WAIT, AUTH, FROM, TO, DATA, BODY, QUIT, END };

struct mg_mgr mgr, mail_mgr;

const char* mail_server = "tcp://ferryfair.com:25";
const char* admin = "Necktwi";
const char* admin_pass = "tornshoes";
const char* to = nullptr;

const char* from = "FerryFair";
char subj[64];
char mesg[128];

bool s_quit = false;
bool sendMail = false;

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

string get_subdomain (const char* host) {
   string hoststr(host);
   if(!config["hostName"]) return string();
   int domainpos =
      hoststr.find(tolower(string((ccp)config["hostName"])).c_str());
   int portpos=hoststr.find(":");
   if (domainpos > 1)
      return hoststr.substr(0, domainpos-1);
   else
      return portpos>1?hoststr.substr(0,portpos):hoststr;
}

void get_data_in_url (const char* url, FFJSON& data) {
   unsigned i = 0;
   unsigned pairStartPin=i;
   string first,second;
   while (url[i]!='\0' && url[i]!='?') ++i;
   pairStartPin=++i;
   while (url[i]!='\0') {
      if(url[i]=='='){
         first=string(url+pairStartPin,i-pairStartPin);
         pairStartPin=i+1;
      } else if (url[i+1]=='&' || url[i+1]=='\0') {
         second=string(url+pairStartPin, i+1-pairStartPin);
         pairStartPin=i+2;
         data[first]=second;
      }
      ++i;
   }
}

void mailfn (
   struct mg_connection *c, int ev, void *ev_data, void *fn_data
) {
   uint8_t *state = (uint8_t *) c->label;
   if (ev == MG_EV_OPEN) {
         // c->is_hexdumping = 1;
      printf("ev open\n");
   } else if (ev == MG_EV_READ) {
      if (c->recv.len > 0 && c->recv.buf[c->recv.len - 1] == '\n') {
         MG_INFO(("<-- %d %s", (int) c->recv.len - 2, c->recv.buf));
         c->recv.len = 0;
         if (*state == EHLO) {
            mg_printf(c, "EHLO %s\r\n", getMachineName().c_str());
            *state = STARTTLS;
         } else if (*state == STARTTLS) {
            mg_printf(c, "STARTTLS\r\n");
            *state = STARTTLS_WAIT;
         } else if (*state == STARTTLS_WAIT) {
            struct mg_tls_opts opts =
               {.ca = "/etc/ssl/certs/ca-certificates.crt"};
            mg_tls_init(c, &opts);
            *state = AUTH;
         } else if (*state == AUTH) {
            char a[100], b[300] = "";
            size_t n = mg_snprintf(a, sizeof(a), "%c%s%c%s", 0, admin, 0,
                                   admin_pass);
            mg_base64_encode((uint8_t *) a, n, b);
            mg_printf(c, "AUTH PLAIN %s\r\n", b);
            *state = FROM;
         } else if (*state == FROM) {
            mg_printf(c, "MAIL FROM: <%s@%s>\r\n", admin,
                      (ccp)config["hostName"]);
            *state = TO;
         } else if (*state == TO) {
            mg_printf(c, "RCPT TO: <%s>\r\n", to);
            *state = DATA;
         } else if (*state == DATA)  {
            mg_printf(c, "DATA\r\n");
            *state = BODY;
         } else if (*state == BODY) {
            mg_printf(c, "From: %s <%s@%s>\r\n", from, admin,
                      (ccp)config["hostName"]);     // Mail header
            mg_printf(c, "Subject: %s\r\n", subj);          // Mail header
            mg_printf(c, "\r\n");                           // End of headers
            mg_printf(c, "%s\r\n", mesg);                   // Mail body
            mg_printf(c, ".\r\n");                          // End of body
            *state = QUIT;
         } else if (*state == QUIT) {
            mg_printf(c, "QUIT\r\n");
            *state = END;
         } else {
            c->is_draining = 1;
            MG_INFO(("end"));
         }
         MG_INFO(("--> %.*s", (int) c->send.len - 2, c->send.buf));
      }
   } else if (ev == MG_EV_CLOSE) {
      s_quit = true;
   } else if (ev == MG_EV_TLS_HS) {
      MG_INFO(("TLS handshake done! Sending EHLO again"));
      mg_printf(c, "EHLO %s\r\n", getMachineName().c_str());
   }
   (void) fn_data, (void) ev_data;
}

void tls_ntls_common (
   struct mg_connection* c, int ev, void* ev_data, void* fn_data
) {
   struct mg_http_serve_opts opts = {
      .root_dir = (ccp)config["homeFolder"]
   };   // Serve local dir
   if (ev == MG_EV_HTTP_MSG) {
      unsigned char b[4];
      b[0] = c->rem.ip & 0xFF;
      b[1] = (c->rem.ip >> 8) & 0xFF;
      b[2] = (c->rem.ip >> 16) & 0xFF;
      b[3] = (c->rem.ip >> 24) & 0xFF;
      printf("Remote IP: %d.%d.%d.%d\n", b[0], b[1], b[2], b[3]);
      struct mg_http_message* hm = (struct mg_http_message*) ev_data;
      printf("hm->uri:\n%s\n", hm->uri.ptr);
      FFJSON sessionData, vhost, user;
      string subdomain;
      ccp referer=nullptr;char proto[8]="https"; int protolen=5;
      const char* username = nullptr,* password = nullptr;
      const char* headers = "content-type: text/json\r\n";
      parseHTTPHeader((ccp)hm->uri.ptr, hm->uri.len, sessionData);
      if (sessionData["Host"]==nullptr) {
         return;
      } else if (
         config["virtualWebHosts"]
         [subdomain=get_subdomain(sessionData["Host"])]
      ){
         printf("subdomain: %s\n",subdomain.c_str());
         vhost=&config["virtualWebHosts"][subdomain];
         //printf("vhost: %s\n", vhost.prettyString().c_str());
         opts.root_dir = (ccp)vhost["rootdir"];
      } else {
         vhost=&config;
      }
      if (!sessionData["Referer"]) goto nextproto;
      referer=sessionData["Referer"];
      protolen = strstr(referer,":") - referer;
      sprintf(proto,"%.*s",protolen,(ccp)sessionData["Referer"]);
     nextproto:
      printf("proto: %s\n",proto);
      printf("Serving: %s\n", opts.root_dir);
      if (!strcmp(sessionData["path"], "/login")) {
         printf("login\n");
         FFJSON body(string(hm->body.ptr, hm->body.len));
         username=body["username"];password=body["password"];
         printf("User: %s\nPass: %s\n", username, password);
         user=&vhost["users"][username];
         cout << (ccp)user["password"] << endl;
         if (user["password"] && !user["inactive"] &&
             !strcmp(password,user["password"])
         ) {
            mg_http_reply(c, 200, headers, "{%Q:%s}", "login","true");
         } else {
            mg_http_reply(c, 200, headers, "{%Q:%s}", "login","false");
         }
      } else if(!strcmp(sessionData["path"], "/signup")){
         ffl_notice(FPL_HTTPSERV, "Signup");
         FFJSON body(string(hm->body.ptr, hm->body.len));
         username=body["username"];password=body["password"];
         ffl_debug(FPL_HTTPSERV, "User: %s\nPass: %s\nEmail: %s",
                   username, password, (ccp)body["email"]);
         user=&vhost["users"][username];
         if (user && !user["inactive"]) {
            ffl_warn(FPL_HTTPSERV, "User already exists.");
            mg_http_reply(c, 200, headers, "{%Q:%d}", "actEmailSent", 0);
            goto done;
         } else if (user["inactive"] && strcmp(body["email"],user["email"])) {
            ffl_warn(FPL_HTTPSERV, "User already exists.");
            mg_http_reply(c, 200, headers, "{%Q:%d}", "actEmailSent", 0);
            goto done;
         } else if (
            user["password"] &&
            vhost["users"][(ccp)body["email"]].val.fptr!=
            &vhost["users"][username]
         ) {
            ffl_warn(FPL_HTTPSERV, "Email already registered.");
            mg_http_reply(c, 200, headers, "{%Q:%d}", "actEmailSent", 0);
            goto done;
         }
         user["password"] = password;
         user["email"] = body["email"];
         vhost["users"][(ccp)user["email"]]=&vhost["users"][username];
         user["inactive"]=true;
         string actKey = random_alphnuma_string();
         user["activationKey"]=actKey;
         printf("actKey: %s\n",actKey.c_str());
         to=user["email"];
         sprintf(subj, "User activation link");
         sprintf(mesg, "Open %s://%s/activate?user=%s&key=%s to activate %s",
                 proto, (ccp)sessionData["Host"], username,
                 (ccp)user["activationKey"], username);
         mg_connect(&mail_mgr, mail_server, mailfn, NULL);
         while(!s_quit)
            mg_mgr_poll(&mail_mgr, 100);
         s_quit=false;
         mg_http_reply(c, 200, headers, "{%Q:%d}", "actEmailSent", 2);
      } else if(strstr((ccp)sessionData["path"], "/activate?")){
         FFJSON data;
         get_data_in_url(sessionData["path"], data);
         username=data["user"];
         user=&vhost["users"][username];
         headers = "content-type: text/plain\r\n";
         if (!user["password"] || !user["inactive"]) {
            mg_http_reply(c, 200, headers, "Wrong key.");
         } else if (!strcmp(user["activationKey"],data["key"])) {
            user["inactive"]=false;
            mg_http_reply(c, 200, headers, "%s activated.", username);
         } else {
            mg_http_reply(c, 200, headers, "Wrong key.");
         }
      } else {
         mg_http_serve_dir(c, (mg_http_message*)ev_data, &opts);
      }
     done: 
      if (valgrind_test)
         force_exit=true;
   }
}

void fn (
   struct mg_connection *c, int ev, void *ev_data, void *fn_data
) {
   tls_ntls_common(c, ev, ev_data, fn_data);
}

void fn_tls (
   struct mg_connection *c, int ev, void *ev_data, void *fn_data
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
) {
   mg_mgr_init(&mgr);
   mg_mgr_init(&mail_mgr);
   char httpsport[16];
   char httpport[16];
   sprintf(httpsport, "0.0.0.0:%d", (int)config["HTTPSPort"]);
   sprintf(httpport, "0.0.0.0:%d", (int)config["HTTPPort"]);
   mg_http_listen(&mgr, httpsport, fn_tls, NULL);
   mg_http_listen(&mgr, httpport, fn, NULL);
   while (!force_exit) {
      mg_mgr_poll(&mgr, 1000);
   }
}

WSServer::~WSServer () {
   
}


//string WSServer::per_session_data__http::vhost();
