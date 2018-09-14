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

#include <libwebsockets.h>
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

#include "lws_config.h"

#include "FerryStream.h"
#include "global.h"
#include "WSServer.h"
#include <ferrybase/JPEGImage.h>
#include <FFJSON.h>
#include <logger.h>
#include <ferrybase/mystdlib.h>
#include <ferrybase/Socket.h>
#include <iostream>
#include <malloc.h>
#include <functional>

using namespace std;

#define LWS_NO_CLIENT

FFJSON            HTTPModel;
int               close_testing;
int               max_poll_elements;
#ifdef EXTERNAL_POLL
lws_pollfd*       pollfds;
int*              fd_lookup;
int               count_pollfds;
#endif
volatile int      dynamic_vhost_enable = 0;
static int        test_options;
struct lws_vhost*        dynamic_vhost;
struct lws_context*      context;
struct lws_plat_file_ops fops_plat;
struct lws_plat_file_ops secure_fops_plat;

#if defined(LWS_WITH_TLS) && defined(LWS_HAVE_SSL_CTX_set1_param)
char crl_path[1024] = "";
#endif

bool validate_path_l (std::string& p) {
   FFJSON::trimWhites (p);
   if (p.length() > 0) {
      return true;
   }
   return false;
}

void test_server_lock(int care)
{
}
void test_server_unlock(int care)
{
}

static void
dump_handshake_info(struct lws *wsi) {
   int n;
   static const char *token_names[] = {
      /*[WSI_TOKEN_GET_URI]		=*/ "GET URI",
      /*[WSI_TOKEN_POST_URI]		=*/ "POST URI",
      /*[WSI_TOKEN_OPTIONS_URI]	=*/ "OPTIONS URI",
      /*[WSI_TOKEN_HOST]		=*/ "Host",
      /*[WSI_TOKEN_CONNECTION]	=*/ "Connection",
      /*[WSI_TOKEN_KEY1]		=*/ "key 1",
      /*[WSI_TOKEN_KEY2]		=*/ "key 2",
      /*[WSI_TOKEN_PROTOCOL]		=*/ "Protocol",
      /*[WSI_TOKEN_UPGRADE]		=*/ "Upgrade",
      /*[WSI_TOKEN_ORIGIN]		=*/ "Origin",
      /*[WSI_TOKEN_DRAFT]		=*/ "Draft",
      /*[WSI_TOKEN_CHALLENGE]		=*/ "Challenge",
      
      /* new for 04 */
      /*[WSI_TOKEN_KEY]		=*/ "Key",
      /*[WSI_TOKEN_VERSION]		=*/ "Version",
      /*[WSI_TOKEN_SWORIGIN]		=*/ "Sworigin",
      
      /* new for 05 */
      /*[WSI_TOKEN_EXTENSIONS]	=*/ "Extensions",
      
      /* client receives these */
      /*[WSI_TOKEN_ACCEPT]		=*/ "Accept",
      /*[WSI_TOKEN_NONCE]		=*/ "Nonce",
      /*[WSI_TOKEN_HTTP]		=*/ "Http",
      
      "Accept:",
      "If-Modified-Since:",
      "Accept-Encoding:",
      "Accept-Language:",
      "Pragma:",
      "Cache-Control:",
      "Authorization:",
      "Cookie:",
      "Content-Length:",
      "Content-Type:",
      "Date:",
      "Range:",
      "Referer:",
      "Uri-Args:",
      
      /*[WSI_TOKEN_MUXURL]	=*/ "MuxURL",
   };
   char buf[256];
   
   for (n = 0; n < sizeof (token_names) / sizeof (token_names[0]); n++) {
      if (!lws_hdr_total_length(wsi, (lws_token_indexes) n))
      continue;
      
      lws_hdr_copy(wsi, buf, sizeof buf, (lws_token_indexes) n);
      
      fprintf(stderr, "    %s %s\n", token_names[n], buf);
   }
}

/* this protocol server (always the first one) just knows how to do HTTP */
const char * get_mimetype(const char *file) {
   int n = strlen(file);
   
   if (n < 5)
   return NULL;
   
   if (!strcmp(&file[n - 4], ".ico"))
   return "image/x-icon";
   
   if (!strcmp(&file[n - 4], ".png"))
   return "image/png";
   
   if (!strcmp(&file[n - 4], ".svg"))
   return "image/svg+xml";
   
   if (!strcmp(&file[n - 5], ".html"))
   return "text/html";
   
   if (!strcmp(&file[n - 4], ".php"))
   return "text/html";
   
   if (!strcmp(&file[n - 3], ".js"))
   return "text/javascript";
   
   if (!strcmp(&file[n - 4], ".css"))
   return "text/css";
   
   if (!strcmp(&file[n - 5], ".json"))
   return "application/json";
   
   if (!strcmp(&file[n - 4], ".txt"))
   return "text/plain";
   
   if (!strcmp(&file[n - 7], ".ffjson"))
   return "text/plain";
   
   if (!strcmp(&file[n - 5], ".woff"))
   return "application/x-font-woff";
   
   if (!strcmp(&file[n - 11], ".safariextz"))
   return "application/octet-stream";
   
   return NULL;
}

unsigned int WSServer::create_session() {
   unsigned int session_id = ++session_count;
   user_session* us = new user_session();
   us->session_id = session_id;
   us->last_access_time = time(NULL);
   user_sessions[session_id] = us;
   return session_id;
}

unsigned int WSServer::valid_session(unsigned int session_id) {
   if (user_sessions.find(session_id) != user_sessions.end()) {
      user_session* us = user_sessions[session_id];
      time_t ct = time(NULL);
      if ((ct - us->last_access_time) <
          (int) config["HttpUserSessionTimeout"]) {
         us->last_access_time = time(NULL);
         return session_id;
      }
   }
   return 0;
}

map<unsigned int, WSServer::user_session*> WSServer::user_sessions;
unsigned int WSServer::session_count = 0;

int WSServer::callback_http (
   struct lws *wsi,
   enum lws_callback_reasons reason,
   void* user,
   void* in, size_t len
) {
#if 0
   char client_name[128];
   char client_ip[128];
#endif
   char                 buf[1024];
   char                 b64[64];
   struct timeval       tv;
   int                  n = 0, m = 0, hlen = 0;
   unsigned char*       p;
   char                 other_headers[1024];
   unsigned char*       end, *start;
   static unsigned char buffer[4096];
   struct stat          stat_buf;
   int            session_id = 0;
   const char*    mimetype;
   FFJSON         ans;
   FFJSON*        ansobj;
   string         sIndexFile = "index.html";
   unsigned long  file_len, amount, sent;
   struct per_session_data__http*   pss =
   (struct per_session_data__http*) user;
   
#ifdef EXTERNAL_POLL
   struct lws_pollargs* pa = (struct lws_pollargs *) in;
#endif
   
   switch (reason) {
      case LWS_CALLBACK_HTTP: {
         lwsl_info ("lws_http_serve: %s\n", in);
         dump_handshake_info (wsi);
         if (len < 1) {
            lws_return_http_status (
               wsi, HTTP_STATUS_BAD_REQUEST, NULL
            );
            goto try_to_reuse;
         }
         #ifndef LWS_NO_CLIENT
         if (!strncmp(in, "/proxytest", 10)) {
            struct lws_client_connect_info i;
            char* rootpath = "/";
            const char *p = (const char *)in;
            if (lws_get_child(wsi))
               break;
            pss->client_finished = 0;
            memset(&i,0, sizeof(i));
            i.context = lws_get_context(wsi);
            i.address = "git.libwebsockets.org";
            i.port = 80;
            i.ssl_connection = 0;
            if (p[10])
            i.path = (char *)in + 10;
            else
            i.path = rootpath;
            i.host = "git.libwebsockets.org";
            i.origin = NULL;
            i.method = "GET";
            i.parent_wsi = wsi;
            i.uri_replace_from = "git.libwebsockets.org/";
            i.uri_replace_to = "/proxytest/";
            if (!lws_client_connect_via_info(&i)) {
               lwsl_err("proxy connect fail\n");
               break;
            }
            break;
         }
         #endif
         /* if a legal POST URL, let it continue and accept data */
         if (lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI))
            return 0;
         
         map<string, string> cookies;
         int cookies_len = lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_COOKIE);
         if (cookies_len) {
            if (cookies_len >= sizeof buf) {
               lws_return_http_status (
                  wsi, HTTP_STATUS_BAD_REQUEST, NULL
               );
               return -1;
            }
            lws_hdr_copy(wsi, buf, sizeof buf, (lws_token_indexes)
                         WSI_TOKEN_HTTP_COOKIE);
            int i = 0;
            int pin = i;
            int pin_length = 0;
            string name;
            string value;
            while (i <= cookies_len) {
               switch (buf[i]) {
                  case '=':
                     pin_length = i - pin;
                     name.assign(buf + pin, pin_length);
                     pin = i + 1;
                     break;
                  case ';':
                  case '\0':
                     pin_length = i - pin;
                     value.assign(buf + pin, pin_length);
                     cookies[name] = value;
                     ffl_debug(FPL_HTTPSERV, "%s:%s", name.c_str(),
                               value.c_str());
                     if (buf[i + 1] == ' ') ++i;
                     pin = i + 1;
                     break;
                  
                  default:
                     break;
               }
               ++i;
            }
         }
         if (cookies.find("session_id") != cookies.end()) {
            session_id = valid_session(stoul(cookies["session_id"]));
         }
         other_headers[0] = '\0';
         if (!session_id) {
            session_id = create_session ();
            sprintf (
               other_headers,
               "Set-Cookie: session_id=%u;Max-Age=360000\x0d\x0a",
               session_id
            );
            ffl_debug (FPL_HTTPSERV, "session %u created", session_id);
         }
         ffl_debug (FPL_HTTPSERV, "session: %u", session_id);
         lws_hdr_copy (wsi, buf, sizeof buf, WSI_TOKEN_HOST);
         string connection (buf);
         domainname = (const char*) buf;
         int dnamenail = -1;
         dnamenail = connection .find ('.');
         ffl_debug(FPL_HTTPSERV, "connection: %s, domainname: %s", buf,
                   domainname.c_str());
         if (dnamenail != string::npos) {
            string vhost;
            vhost = connection.substr(0, dnamenail);
            ffl_debug(FPL_HTTPSERV, "vhost: %s", vhost.c_str());
            strncpy(pss->vhost, vhost.c_str(), vhost.length());
            pss->vhost[vhost.length()] = '\0';
         }
         
         string lResourcePath;
         if (!config["virtualWebHosts"][(const char*)pss->vhost])
         strncpy(pss->vhost, "www", 4);
         lResourcePath.assign((const char*)
            config["virtualWebHosts"][(const char*)pss->vhost]["rootdir"]);
         
         if (!models[(const char*)pss->vhost]) {
            std::ifstream t(string((const char*) config["virtualWebHosts"]
               [(const char*)pss->vhost]["rootdir"]) + "/model.json", ios::in);
            if (t.is_open()) {
               std::string str((std::istreambuf_iterator<char>(t)),
                               std::istreambuf_iterator<char>());
               models[(const char*)pss->vhost].init(str);
            }
         }
         if(models[(const char*)pss->vhost]["indexFile"]){
            sIndexFile=(const char*)models[(const char*)pss->vhost]["indexFile"];
            ffl_debug(FPL_HTTPSERV, "indexFile: %s", sIndexFile.c_str());
         }
         
         // exit if there is an attempt to access parent directory
         if (strstr((char*) in, "/..")) {
            lws_return_http_status(wsi,
                                   HTTP_STATUS_FORBIDDEN, NULL);
            return -1;
         }
         
         /* if a legal POST URL, let it continue and accept data */
         if (lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI))
         return 0;
         
         /* check for the "send a big file by hand" example case */
         
         if (strcmp((const char *) in, "/model.ffjson") == 0 ||
             strcmp((const char*) in, "/model.json") == 0) {
            pss->payload = new string(models[(const char*)pss->vhost].stringify(true));
            goto sendJSONPayload;
         }
         
         strcpy(buf, lResourcePath.c_str());
         strncat((char*) buf, (const char*) in, sizeof (buf) - lResourcePath.length());
         buf[sizeof (buf) - 1] = '\0';
         p = buffer+LWS_PRE;
         end = p + sizeof(buffer) - LWS_PRE;
         ffl_debug(FPL_HTTPSERV, "Sending %s", buf);
         char* pExtNail = strrchr(buf, '.');
         if(pExtNail && *(pExtNail-1)=='/')pExtNail=nullptr;
         string location;
         if (((const char*) in)[len - 1] == '/') {
            strcat(buf, sIndexFile.c_str());
         }
         else if(!pExtNail){
            if (lws_add_http_header_status(wsi, 301, &p, end))
            return 1;
            if (lws_is_ssl(wsi))
            location = "https://";
            else
            location = "http://";
            struct stat st;
            location += domainname + (const char*)in;
            if(stat(buf, &st) == 0)
            if(!S_ISDIR(st.st_mode))
            goto endofextnail;
            location += "/index.html";
            if(lws_add_http_header_by_name(wsi,
                                           (unsigned char *) "Location:",
                                           (unsigned char *)location.c_str(),
                                           location.length(), &p, end))
            return 1;
            if (lws_finalize_http_header(wsi, &p, end))
            return 1;
            
            *p = '\0';
            lwsl_info("%s\n", buffer + LWS_PRE);
            n = lws_write(wsi, buffer+LWS_PRE,
                          p - (buffer+LWS_PRE), LWS_WRITE_HTTP_HEADERS);
            
            if (n < 0) {
               return -1;
            }
            goto try_to_reuse;
            //strcat(buf, "/index.html");
         }
      endofextnail:
         int ihttpport = config["virtualWebHosts"][(const char*)pss->vhost]["redirectHTTPPortTo"];
         int ihttpsport = config["virtualWebHosts"][(const char*)pss->vhost]["redirectHTTPSPortTo"];
         bool bToHTTPS = config["virtualWebHosts"][(const char*)pss->vhost]["toHTTPS"];
         if(((ihttpport||bToHTTPS)&&!lws_is_ssl(wsi)) || (ihttpsport&&lws_is_ssl(wsi))){
            if (lws_add_http_header_status(wsi, 301, &p, end))
            return 1;
            if (lws_is_ssl(wsi)) {
               location = "https://";
               location += domainname + ":" + to_string(ihttpsport) + (const char*)in;
            }else{
               location = bToHTTPS?"https://":"http://";
               location += domainname + ":" + to_string(bToHTTPS?ihttpsport:ihttpport) + (const char*)in;
            }
            if(lws_add_http_header_by_name(wsi,
                                           (unsigned char *) "Location:",
                                           (unsigned char *)location.c_str(),
                                           location.length(), &p, end))
            return 1;
            if (lws_finalize_http_header(wsi, &p, end))
            return 1;
            
            *p = '\0';
            lwsl_info("%s\n", buffer + LWS_PRE);
            n = lws_write(wsi, buffer+LWS_PRE,
                          p - (buffer+LWS_PRE), LWS_WRITE_HTTP_HEADERS);
            
            if (n < 0) {
               return -1;
            }
            goto try_to_reuse;
            
         }
         ffl_debug(FPL_HTTPSERV, "Location: %s", location.c_str());
         /* refuse to serve files we don't understand */
         mimetype = get_mimetype(buf);
         if (!mimetype) {
            if(!pExtNail){
               mimetype = "text/plain";
            }
            else {
               lwsl_err("Unknown mimetype for %s\n", buf);
               lws_return_http_status(wsi,
                                      HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE, NULL);
               return -1;
            }
         }
         if (strcmp(mimetype, "text/x-php")==0){
            string sPHPCMD("php ");
            sPHPCMD+=buf;
            pss->payload=new string(getStdoutFromCommand(sPHPCMD));
            file_len = pss->payload->length();
         } else{
            
            //pss->fd = open(buf, O_RDONLY | _O_BINARY);
            //pss->fd=lws_plat_file_open(wsi, buf, &file_len,
            //                           LWS_O_RDONLY);
            
            //if (pss->fd == LWS_INVALID_FILE) {
            //   ffl_debug(FPL_HTTPSERV, "unable to open file");
            //   return -1;
            //}
         }
         if (lws_add_http_header_status(wsi, 200, &p, end))
         return 1;
         if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_SERVER,
                                          (unsigned char *)"libwebsockets",
                                          13, &p, end))
         return 1;
         if (lws_add_http_header_by_token(wsi,
                                          WSI_TOKEN_HTTP_CONTENT_TYPE,
                                          (unsigned char *)mimetype,
                                          strlen(mimetype), &p, end))
         return 1;
         if (lws_add_http_header_content_length(wsi,
                                                file_len, &p,
                                                end))
         return 1;
         p += sprintf((char *) p, "%s", other_headers);
         if (lws_is_ssl(wsi) && lws_add_http_header_by_name(wsi,
            (unsigned char *) "Strict-Transport-Security:",
            (unsigned char *)"max-age=15768000 ; includeSubDomains",
            36, &p, end
         )) {
            return 1;
         }
         if (lws_finalize_http_header(wsi, &p, end))
            return 1;
         
         *p = '\0';
         lwsl_info("%s\n", buffer + LWS_PRE);
         
         /*
          * send the http headers...
          * this won't block since it's the first payload sent
          * on the connection since it was established
          * (too small for partial)
          */
         n = lws_write(wsi, buffer+LWS_PRE,
                       p - (buffer+LWS_PRE), LWS_WRITE_HTTP_HEADERS);
         
         if (n < 0) {
            //lws_plat_file_close(wsi, pss->fd);
            return -1;
         }
         
         /*
          * book us a LWS_CALLBACK_HTTP_WRITEABLE callback
          */
         lws_callback_on_writable(wsi);
         break;
      }
      
      case LWS_CALLBACK_HTTP_BODY:
      {
         if (!pss->payload) {
            pss->payload = new string();
         }
         //			strncpy((char*) buf, (const char*) in, 80);
         //			buf[80] = '\0';
         //			if (len < 80)
         //				buf[len] = '\0';
         pss->payload->append((const char*) in, len);
         lwsl_notice("LWS_CALLBACK_HTTP_BODY: %s... len %d\n",
                     (const char *) pss->payload->c_str(), (int) len);
         
         
         break;
      }
      
      case LWS_CALLBACK_HTTP_BODY_COMPLETION:
      {
         lwsl_notice("LWS_CALLBACK_HTTP_BODY_COMPLETION\n");
         /* the whole of the sent body arried, close the connection */
         
         ans.init(*pss->payload);
         ffl_debug(FPL_HTTPSERV, "%s", ans.stringify().c_str());
         
         ansobj = models[(const char*)pss->vhost].answerObject(&ans);
         pss->payload->assign(ansobj->stringify(true));
         delete ansobj;
      sendJSONPayload:
         ffl_debug(FPL_HTTPSERV, "%s", pss->payload->c_str());
         
         if (lws_add_http_header_status(wsi, 200, &p, end))
         return 1;
         if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_SERVER,
                                          (unsigned char *)"libwebsockets",
                                          13, &p, end))
         return 1;
         if (lws_add_http_header_by_token(wsi,
                                          WSI_TOKEN_HTTP_CONTENT_TYPE,
                                          (unsigned char *)mimetype,
                                          strlen(mimetype), &p, end))
         return 1;
         if (lws_add_http_header_content_length(wsi,
                                                file_len, &p,
                                                end))
         return 1;
         if (lws_finalize_http_header(wsi, &p, end))
         return 1;
         
         *p = '\0';
         lwsl_info("%s\n", buffer + LWS_PRE);
         
         /*
          * send the http headers...
          * this won't block since it's the first payload sent
          * on the connection since it was established
          * (too small for partial)
          */
         
         n = lws_write(wsi, buffer + LWS_PRE,
                       p - (buffer + LWS_PRE),
                       LWS_WRITE_HTTP_HEADERS);
         if (n < 0) {
            //lws_plat_file_close(wsi, pss->fd);
            delete pss->payload;
            return -1;
         }
         pss->offset = pss->payload->c_str();
         /*
          * book us a LWS_CALLBACK_HTTP_WRITEABLE callback
          */
         lws_callback_on_writable(wsi);
         break;
      }
      
      case LWS_CALLBACK_HTTP_FILE_COMPLETION:
      {
         //		lwsl_info("LWS_CALLBACK_HTTP_FILE_COMPLETION seen\n");
         /* kill the connection after we sent one file */
         return -1;
      }
      case LWS_CALLBACK_HTTP_WRITEABLE:
      {
         lwsl_info("LWS_CALLBACK_HTTP_WRITEABLE\n");
         
         
         if (pss->fd == LWS_INVALID_FILE && !pss->payload)
         goto try_to_reuse;
         /*
          * we can send more of whatever it is we were sending
          */
         do {
            n = sizeof(buffer) - LWS_PRE;
            
            /* but if the peer told us he wants less, we can adapt */
            m = lws_get_peer_write_allowance(wsi);
            
            /* -1 means not using a protocol that has this info */
            if (m == 0)
            /* right now, peer can't handle anything */
            goto later;
            
            if (m != -1 && m < n)
            /* he couldn't handle that much */
            n = m;
            
            if(pss->fd){
               //n = lws_plat_file_read(wsi, pss->fd,
               //                       &amount, buffer + LWS_PRE, n);
            }else{
               n= pss->payload->length()>n?n:pss->payload->length();
               strncpy((char*)(buffer + LWS_PRE), pss->payload->c_str(), n);
               pss->payload->assign(pss->payload->substr(n));
               amount = n;
            }
            /* problem reading, close conn */
            if (n < 0){
               lwsl_err("problem reading file\n");
               goto bail;
            }
            n = (int)amount;
            /* sent it all, close conn */
            if (n == 0)
            goto penultimate;
            /*
             * because it's HTTP and not websocket, don't need to take
             * care about pre and postamble
             */
            m = lws_write(wsi, buffer + LWS_PRE, n, LWS_WRITE_HTTP);
            if (m < 0){
               lwsl_err("write failed\n");
               /* write failed, close conn */
               goto bail;
            }
            if (m) /* while still active, extend timeout */
            lws_set_timeout(wsi, PENDING_TIMEOUT_HTTP_CONTENT, 5);
            sent += m;
            
         } while (!lws_send_pipe_choked(wsi));
         
      later:
         lws_callback_on_writable(wsi);
         break;
      flushbail:
      penultimate:
         if (pss->fd) {
            //lws_plat_file_close(wsi, pss->fd);
            pss->fd = LWS_INVALID_FILE;
         } else {
            delete pss->payload;
         }
         goto try_to_reuse;
         
         
      bail:
         if (pss->payload != NULL) {
            delete pss->payload;
         }
         //lws_plat_file_close(wsi, pss->fd);
         return -1;
      }
      /*
       * callback for confirming to continue with client IP appear in
       * protocol 0 callback since no websocket protocol has been agreed
       * yet.  You can just ignore this if you won't filter on client IP
       * since the default unhandled callback return is 0 meaning let the
       * connection continue.
       */
      case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
      /* if we returned non-zero from here, we kill the connection */
      break;
      
      
      /*
       * callbacks for managing the external poll() array appear in
       * protocol 0 callback
       */
      
      case LWS_CALLBACK_LOCK_POLL:
      /*
       * lock mutex to protect pollfd state
       * called before any other POLL related callback
       */
      test_server_lock(len);
      break;
      
      case LWS_CALLBACK_UNLOCK_POLL:
      /*
       * unlock mutex to protect pollfd state when
       * called after any other POLL related callback
       */
      test_server_unlock(len);
      break;
#ifdef EXTERNAL_POLL
      case LWS_CALLBACK_ADD_POLL_FD:
      
      if (count_pollfds >= max_poll_elements) {
         lwsl_err("LWS_CALLBACK_ADD_POLL_FD: too many sockets to track\n");
         return 1;
      }
      
      fd_lookup[pa->fd] = count_pollfds;
      pollfds[count_pollfds].fd = pa->fd;
      pollfds[count_pollfds].events = pa->events;
      pollfds[count_pollfds++].revents = 0;
      break;
      
      case LWS_CALLBACK_DEL_POLL_FD:
      if (!--count_pollfds)
      break;
      m = fd_lookup[pa->fd];
      /* have the last guy take up the vacant slot */
      pollfds[m] = pollfds[count_pollfds];
      fd_lookup[pollfds[count_pollfds].fd] = m;
      break;
      
      case LWS_CALLBACK_CHANGE_MODE_POLL_FD:
      pollfds[fd_lookup[pa->fd]].events = pa->events;
      break;
      
#endif
      
      case LWS_CALLBACK_GET_THREAD_ID:
      /*
       * if you will call "libwebsocket_callback_on_writable"
       * from a different thread, return the caller thread ID
       * here so lws can use this information to work out if it
       * should signal the poll() loop to exit and restart early
       */
      
      /* return pthread_getthreadid_np(); */
      
      break;
#if defined(LWS_USE_POLARSSL)
#else
#if defined(LWS_USE_MBEDTLS)
#else
#if defined(LWS_OPENSSL_SUPPORT)
      case LWS_CALLBACK_OPENSSL_PERFORM_CLIENT_CERT_VERIFICATION:
      /* Verify the client certificate */
      if (!len || (SSL_get_verify_result((SSL*)in) != X509_V_OK)) {
         int err = X509_STORE_CTX_get_error((X509_STORE_CTX*)user);
         int depth = X509_STORE_CTX_get_error_depth((X509_STORE_CTX*)user);
         const char* msg = X509_verify_cert_error_string(err);
         lwsl_err("LWS_CALLBACK_OPENSSL_PERFORM_CLIENT_CERT_VERIFICATION: SSL error: %s (%d), depth: %d\n", msg, err, depth);
         return 1;
      }
      break;
#if defined(LWS_HAVE_SSL_CTX_set1_param)
      case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS:
      if (crl_path[0]) {
         /* Enable CRL checking */
         X509_VERIFY_PARAM *param = X509_VERIFY_PARAM_new();
         X509_VERIFY_PARAM_set_flags(param, X509_V_FLAG_CRL_CHECK);
         SSL_CTX_set1_param((SSL_CTX*)user, param);
         X509_STORE *store = SSL_CTX_get_cert_store((SSL_CTX*)user);
         X509_LOOKUP *lookup = X509_STORE_add_lookup(store, X509_LOOKUP_file());
         n = X509_load_cert_crl_file(lookup, crl_path, X509_FILETYPE_PEM);
         X509_VERIFY_PARAM_free(param);
         if (n != 1) {
            char errbuf[256];
            n = ERR_get_error();
            lwsl_err("LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS: SSL error: %s (%d)\n", ERR_error_string(n, errbuf), n);
            return 1;
         }
      }
      break;
#endif
#endif
#endif
#endif
      
      default:
      break;
   }
   return 0;
try_to_reuse:
   if (lws_http_transaction_completed(wsi))
   return -1;
   
   return 0;
}

int WSServer::callbackFairPlayWS (
   struct lws *wsi, enum lws_callback_reasons reason,
   void *user, void *in, size_t len
) {
   struct per_session_data__fairplay *pss =
   (struct per_session_data__fairplay *) user;
   int n;
   std::list<FFJSON*>::iterator ii;
   switch (reason) {
      
#ifndef LWS_NO_SERVER
      
      case LWS_CALLBACK_ESTABLISHED:
      {
         ffl_notice(FPL_WSSERV, "viewer %s:%d connected",
                    Socket::getIpAddr(lws_get_socket_fd(wsi)).c_str(),
                    Socket::getPort(lws_get_socket_fd(wsi)));
         pss->state = FRAGSTATE_NEW_PCK;
         break;
      }
      /* when the callback is used for server operations --> */
      
      case LWS_CALLBACK_SERVER_WRITEABLE:
      switch (pss->state) {
         case FRAGSTATE_INIT_PCK:
         {
            pss->payload = new string("{\"HuffmanTable\":\"");
            pss->payload->append((const char*) b64_hmt, b64_hmt_l);
            pss->payload->append("\"}");
            if (pss->payload->length() < MAX_ECHO_PAYLOAD) {
               n = lws_write(wsi, (unsigned char*) pss->payload->c_str(),
                             pss->payload->length(), LWS_WRITE_TEXT);
               pss->state = FRAGSTATE_NEW_PCK;
               delete pss->payload;
               break;
            } else {
               pss->initByte = (unsigned char*) pss->payload->c_str();
               n = lws_write(wsi, pss->initByte, MAX_ECHO_PAYLOAD,
                             (lws_write_protocol) (LWS_WRITE_TEXT |
                                                   LWS_WRITE_NO_FIN));
               pss->initByte += MAX_ECHO_PAYLOAD;
               pss->state = FRAGSTATE_MORE_FRAGS;
               pss->deletePayload = true;
            }
            if (n < 0) {
               lwsl_err("ERROR %d writing to socket, hanging up\n", n);
               return 1;
            }
            if (n < (int) pss->len) {
               lwsl_err("Partial write\n");
               return -1;
            }
            lws_callback_on_writable(wsi);
            break;
         }
         case FRAGSTATE_NEW_PCK:
         {
            //If incoming packets arrive after websocket on the path
            if (pss->i == pss->packs->end()) {
               if (pss->packs->size() == 0)break;
               pss->i = pss->packs->begin();
            }
            
            ii = pss->i;
            ii++;
            if (pss->endHit) {
               pss->endHit = false;
               if (ii != pss->packs->end()) {
                  pss->i++;
                  ii++;
               } else {
                  ffl_debug(FPL_WSSERV,
                            "unexpected callback for frame %d on ws %p"
                            " with path %s", (int) (**pss->i)["index"],
                            wsi, id_path_map[wsi_path_map[wsi]].c_str());
                  pss->endHit = true;
                  break;
               }
            }
            std::string* pl = pack_string_map[*pss->i];
            if (pl->length() < MAX_ECHO_PAYLOAD) {
               n = lws_write(wsi, (unsigned char*) pl->c_str(),
                             pl->length(), LWS_WRITE_TEXT);
            } else {
               pss->payload = pl;
               pss->initByte = (unsigned char*) pl->c_str();
               n = lws_write(wsi, pss->initByte, MAX_ECHO_PAYLOAD,
                             (lws_write_protocol) (LWS_WRITE_TEXT |
                                                   LWS_WRITE_NO_FIN));
               pss->initByte += n;
               pss->state = FRAGSTATE_MORE_FRAGS;
               lws_callback_on_writable(wsi);
            }
            ffl_debug(FPL_WSSERV, "frame index %d sent to %p",
                      (int) (**pss->i)["index"], wsi);
            if (n < 0) {
               lwsl_err("ERROR %d writing to socket, hanging up\n", n);
               return 1;
            }
            int iR8PLSize = MAX_ECHO_PAYLOAD > pl->length() ?
            pl->length() : MAX_ECHO_PAYLOAD;
            if (n < iR8PLSize) {
               lwsl_err("Partial write\n");
               return -1;
            }
            if (ii != pss->packs->end()) {
               pss->i++;
               lws_callback_on_writable(wsi);
            } else {
               pss->endHit = true;
            }
            break;
         }
         case FRAGSTATE_SEND_ERRMSG:
         {
            if (pss->payload->length() < MAX_ECHO_PAYLOAD) {
               n = lws_write(wsi, (unsigned char*)
                             pss->payload->c_str(), pss->payload->length(),
                             LWS_WRITE_TEXT);
            } else {
               ffl_err(FPL_WSSERV,
                       "error msg greater than MAX_ECHO_PAYLOAD "
                       "is being sent. Verify ur code!");
            }
            pss->state = FRAGSTATE_NEW_PCK;
            delete pss->payload;
            if (n < 0) {
               lwsl_err("ERROR %d writing to socket, hanging up\n", n);
               return 1;
            }
            if (n < (int) pss->len) {
               lwsl_err("Partial write\n");
               return -1;
            }
            break;
         }
         
         case FRAGSTATE_MORE_FRAGS:
         {
            int slength = (reinterpret_cast<uintptr_t> (pss->initByte)
                           - reinterpret_cast<uintptr_t> (pss->payload->c_str()));
            int length = (pss->payload->length() - slength);
            int rlength = length - MAX_ECHO_PAYLOAD;
            length = length > MAX_ECHO_PAYLOAD ? MAX_ECHO_PAYLOAD : length;
            n = lws_write(wsi, pss->initByte, length,
                          (lws_write_protocol) (rlength > 0 ?
                                                (LWS_WRITE_CONTINUATION | LWS_WRITE_NO_FIN) :
                                                LWS_WRITE_CONTINUATION));
            pss->initByte += n;
            if (n < 0) {
               lwsl_err("ERROR %d writing to socket, hanging up\n", n);
               if (pss->deletePayload) {
                  pss->deletePayload = false;
                  delete pss->payload;
                  pss->payload = NULL;
               }
               return 1;
            }
            if (n < (int) length) {
               lwsl_err("Partial write\n");
               if (pss->deletePayload) {
                  pss->deletePayload = false;
                  delete pss->payload;
                  pss->payload = NULL;
               }
               return -1;
            }
            length = slength + length;
            if (rlength <= 0) {
               pss->initByte == NULL;
               pss->state = FRAGSTATE_NEW_PCK;
               if (pss->deletePayload) {
                  pss->deletePayload = false;
                  delete pss->payload;
               }
            } else {
               lws_callback_on_writable(wsi);
            }
            break;
         }
         default:
         break;
      }
      break;
      
      case LWS_CALLBACK_RECEIVE:
      if (len > MAX_ECHO_PAYLOAD) {
         lwsl_err("Server received packet bigger than %u, hanging up\n",
                  MAX_ECHO_PAYLOAD);
         return 1;
      }
      //memcpy(&pss->buf[LWS_SEND_BUFFER_PRE_PADDING], in, len);
      //pss->len = (unsigned int) len;
      //lws_callback_on_writable(context, wsi);
      switch (pss->state) {
         case FRAGSTATE_NEW_PCK:
         pss->payload = new std::string();
         pss->state = FRAGSTATE_MORE_FRAGS;
         case FRAGSTATE_MORE_FRAGS:
         pss->payload->append((char*) in);
         if (lws_remaining_packet_payload(wsi) == 0 &&
             lws_is_final_fragment(wsi)) {
            ffl_debug(FPL_WSSERV, (const char*) pss->payload->c_str());
            FFJSON ffjson(*pss->payload);
            delete pss->payload;
            pss->payload = NULL;
            std::string path = std::string((const char*) (ffjson["path"]),
                                           ffjson["path"].size);
            int pathId;
            int bufferSize;
            if (validate_path_l(path)) {
               pathId = init_path(path);
               if ((wsi_path_map).find(wsi) != wsi_path_map.end()) {
                  if ((wsi_path_map)[wsi] == pathId) {
                     //ignore
                  }
               } else {
                  (wsi_path_map)[wsi] = pathId;
                  if (path_packs_map.find(pathId) != path_packs_map.end()) {
                     pss->packs = path_packs_map[pathId];
                     path_wsi_map[pathId]->push_back(wsi);
                     bufferSize = (int) ffjson["bufferSize"];
                     ffl_debug(FPL_WSSERV, "client has buffer size %d",
                               bufferSize);
                     //validate input data
                     bufferSize = (bufferSize > 0)&&(packBufSize >= bufferSize)
                     ? bufferSize : packBufSize;
                     int initpck = (initpck = (pss->packs->size()
                                               - bufferSize)) > 0 ? initpck : 0;
                     pss->i = pss->packs->begin();
                     std::advance(pss->i, initpck);
                     pss->state = FRAGSTATE_INIT_PCK;
                     lws_callback_on_writable(wsi);
                  }
               }
            } else {
               pss->payload = new std::string("{\"error\":\"Illegal path\"}");
               pss->state = FRAGSTATE_SEND_ERRMSG;
               //memcpy(&pss->buf[LWS_SEND_BUFFER_PRE_PADDING],
               //response, strlen(response));
               lws_callback_on_writable(wsi);
            }
         }
         break;
      }
      break;
      
      case LWS_CALLBACK_CLOSED:
      {
         std::map<lws*, int>::iterator i;
         i = wsi_path_map.begin();
         int path = 0;
         while (i != wsi_path_map.end()) {
            if (wsi == i->first) {
               path = i->second;
               wsi_path_map.erase(i);
               break;
            }
         }
         if (path != 0) {
            std::list<lws*>& wsil = *(path_wsi_map)[path];
            std::list<lws*>::iterator j = wsil.begin();
            while (j != wsil.end()) {
               if (*j == wsi) {
                  wsil.erase(j);
                  break;
               }
               j++;
            }
         }
      }
      break;
#endif
      
#ifndef LWS_NO_CLIENT
      /* when the callback is used for client operations --> */
      
      case LWS_CALLBACK_CLIENT_ESTABLISHED:
      lwsl_notice("Client has connected\n");
      pss->index = 0;
      break;
      
      case LWS_CALLBACK_CLIENT_RECEIVE:
      lwsl_notice("Client RX: %s", (char *) in);
      break;
      
      case LWS_CALLBACK_CLIENT_WRITEABLE:
      /* we will send our packet... */
      pss->len = sprintf((char *) &pss->buf[LWS_SEND_BUFFER_PRE_PADDING], "hello from libwebsockets-test-echo client pid %d index %d\n", getpid(), pss->index++);
      lwsl_notice("Client TX: %s", &pss->buf[LWS_SEND_BUFFER_PRE_PADDING]);
      n = lws_write(wsi, &pss->buf[LWS_SEND_BUFFER_PRE_PADDING], pss->len, LWS_WRITE_TEXT);
      if (n < 0) {
         lwsl_err("ERROR %d writing to socket, hanging up\n", n);
         return -1;
      }
      if (n < (int) pss->len) {
         lwsl_err("Partial write\n");
         return -1;
      }
      break;
#endif
      default:
      
      break;
   }
   return 0;
}

static lws_fop_fd_t fops_open (
   const struct lws_plat_file_ops* fops, const char* vfs_path,
   const char* vpath, lws_fop_flags_t* flags
) {
   lws_fop_fd_t fop_fd;

   /* call through to original platform implementation */
   fop_fd = fops_plat .open (fops, vfs_path, vpath, flags);

   if (fop_fd)
      lwsl_info ("%s: opening %s, ret %p, len %lu\n", __func__,
         vfs_path, fop_fd, (long)lws_vfs_get_length(fop_fd)
      );
   else
      lwsl_info ("%s: open %s failed\n", __func__, vfs_path);

   return fop_fd;
}

static lws_fop_fd_t secure_fops_open (
   const struct lws_plat_file_ops* fops, const char* vfs_path,
   const char* vpath, lws_fop_flags_t* flags
) {
   lws_fop_fd_t fop_fd;

   /* call through to original platform implementation */
   fop_fd = fops_plat .open (fops, vfs_path, vpath, flags);

   if (fop_fd)
      lwsl_info ("%s: opening %s, ret %p, len %lu\n", __func__,
         vfs_path, fop_fd, (long)lws_vfs_get_length(fop_fd)
      );
   else
      lwsl_info ("%s: open %s failed\n", __func__, vfs_path);

   return fop_fd;
}

void sighandler (int sig) {
   if (force_exit != 1) {
      force_exit = 1;
   } else {
      exit(0);
   }
}

static struct option options[] = {
   { "help", no_argument, NULL, 'h'},
   { "debug", required_argument, NULL, 'd'},
   { "port", required_argument, NULL, 'p'},
#ifndef LWS_NO_CLIENT
   { "client", required_argument, NULL, 'c'},
   { "ratems", required_argument, NULL, 'r'},
#endif
   { "ssl", no_argument, NULL, 's'},
   { "interface", required_argument, NULL, 'i'},
#ifndef LWS_NO_DAEMONIZE
   { "daemonize", no_argument, NULL, 'D'},
#endif
   { NULL, 0, 0, 0}
};

static const struct lws_extension exts[] = {{
      "permessage-deflate",
      lws_extension_callback_pm_deflate,
      "permessage-deflate"
   }, {
      "deflate-frame",
      lws_extension_callback_pm_deflate,
      "deflate_frame"
   },
   { NULL, NULL, NULL /* terminator */ }
};

WSServer::WSServer (
   const char* pcHostName, int iDebugLevel,
   int iPort, int iSecurePort, const char* pcSSLCertFilePath,
   const char* pcSSLPrivKeyFilePath, const char* pcSSLCAFilePath,
   bool bDaemonize, int iRateUs, const char* pcInterface,
   const char* pcClient, int iOpts,
   #ifndef LWS_NO_CLIENT
      const char* pcAddress, unsigned int uiOldus, struct lws* pWSI,
   #endif
   int iSysLogOptions
) :
   m_iDebugLevel(iDebugLevel),
   m_iPort(iPort),
   m_iSecurePort(iSecurePort),
   m_bDaemonize(bDaemonize),
   m_iRateUs(iRateUs),
   m_iOpts(iOpts),
   #ifndef LWS_NO_CLIENT
      m_uiOldus(uiOldus),
      m_pWSI(pWSI),
   #endif
   m_iSysLogOptions(iSysLogOptions)
{
   strcpy(m_pcHostName,pcHostName);
   strcpy(m_pcSSLCertFilePath,pcSSLCertFilePath);
   strcpy(m_pcSSLPrivKeyFilePath,pcSSLPrivKeyFilePath);
   strcpy(m_pcSSLCAFilePath,pcSSLPrivKeyFilePath);
   strcpy(m_pcInterface,pcInterface);
   #ifndef LWS_NO_CLIENT
      strcpy(m_pcClient,pcClient);
      strcpy(m_pcAddress,pcAddress);
   #endif
      #ifndef WIN32
      m_iSysLogOptions = LOG_PID | LOG_PERROR;
   #endif
   #ifndef LWS_NO_CLIENT
      m_iRateUs = 250000;
   #endif
   memset(&m_Info, 0, sizeof m_Info);
   #ifndef LWS_NO_CLIENT
      lwsl_notice("Built to support client operations\n");
   #endif
   #ifndef LWS_NO_SERVER
      lwsl_notice("Built to support server operations\n");
   #endif
   #ifndef LWS_NO_CLIENT
   #endif
   
   #ifndef LWS_NO_CLIENT
      if (*m_pcClient) {
         strcpy(m_pcAddress, m_pcClient);
         m_iPort = 80;
      }
   m_iRateUs = m_iRateUs * 1000;
   #endif
   m_Info.vhost_name=m_pcHostName;
   m_Info.port = m_iPort;
   m_Info.iface = *m_pcInterface ? m_pcInterface : NULL;
   m_Info.protocols = protocols;
   m_Info.gid = -1;
   m_Info.uid = -1;
   m_Info.options = m_iOpts | LWS_SERVER_OPTION_VALIDATE_UTF8
   | LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT
   | LWS_SERVER_OPTION_VALIDATE_UTF8;
   m_Info.max_http_header_pool = 16;
   m_Info.extensions = exts;
   m_Info.timeout_secs = 5;
   if(m_iSecurePort
#ifndef LWS_NO_CLIENT
      && !*m_pcClient
#endif
      ){
      m_SecureInfo=m_Info;
      m_SecureInfo.port=m_iSecurePort;
      m_SecureInfo.ssl_cipher_list = 
      "ECDHE-ECDSA-AES256-GCM-SHA384:"
      "ECDHE-RSA-AES256-GCM-SHA384:"
      "DHE-RSA-AES256-GCM-SHA384:"
      "ECDHE-RSA-AES256-SHA384:"
      "HIGH:!aNULL:!eNULL:!EXPORT:"
      "!DES:!MD5:!PSK:!RC4:!HMAC_SHA1:"
      "!SHA1:!DHE-RSA-AES128-GCM-SHA256:"
      "!DHE-RSA-AES128-SHA256:"
      "!AES128-GCM-SHA256:"
      "!AES128-SHA256:"
      "!DHE-RSA-AES256-SHA256:"
      "!AES256-GCM-SHA384:"
      "!AES256-SHA256";
      
      //m_SecureInfo.options |= LWS_SERVER_OPTION_REDIRECT_HTTP_TO_HTTPS;
      m_SecureInfo.ssl_cert_filepath = m_pcSSLCertFilePath;
      m_SecureInfo.ssl_private_key_filepath = m_pcSSLPrivKeyFilePath;
      m_SecureInfo.ssl_ca_filepath = m_pcSSLCAFilePath;
      
   }
   heartThread = new thread(bind(&WSServer::heart,this));
}

WSServer::~WSServer() {
   heartThread->join();
   delete heartThread;
}

FFJSON WSServer::models(FFJSON::OBJECT);

int WSServer::heart() {
   int n=0,N = 0;
#ifndef LWS_NO_DAEMONIZE
   if (!*m_pcClient && m_bDaemonize && lws_daemonize("/tmp/.lwstecho-lock")) {
      fprintf(stderr, "Failed to daemonize\n");
      return 1;
   }
#endif
   
#ifndef _WIN32
   /* we will only try to log things according to our debug_level */
   setlogmask(LOG_UPTO(LOG_DEBUG));
   openlog("lwsts", m_iSysLogOptions, LOG_DAEMON);
#endif
   /* tell the library what debug level to emit and to send it to syslog */
   lws_set_log_level(m_iDebugLevel, lwsl_emit_syslog);
   lwsl_notice("libwebsockets test server - license LGPL2.1+SLE\n");
   lwsl_notice("(C) Copyright 2010-2016 Andy Green <andy@warmcat.com>\n");
#ifndef LWS_NO_CLIENT
   if (*m_pcClient) {
      lwsl_notice("Running in client mode\n");
      m_iPort = CONTEXT_PORT_NO_LISTEN;
   } else {
#endif
#ifndef LWS_NO_SERVER
      lwsl_notice("Running in server mode\n");
#endif
#ifndef LWS_NO_CLIENT
   }
#endif
   m_pContext = lws_create_context(&m_Info);
   if (m_pContext==NULL) {
      lwsl_err("libwebsocket init failed\n");
      return -1;
   }
   if (m_iSecurePort
#ifndef LWS_NO_CLIENT
      && !*m_pcClient
#endif
   ) {
      m_pSecureContext = lws_create_context(&m_SecureInfo);
      if (m_pSecureContext == NULL) {
         lwsl_err("libwebsocket init failed\n");
         return -1;
      }
   }
   fops_plat = *(lws_get_fops(m_pContext));
   lws_get_fops(m_pContext)->open = fops_open;
   secure_fops_plat = *(lws_get_fops(m_pSecureContext));
   lws_get_fops(m_pSecureContext)->open = secure_fops_open;
   
#ifndef LWS_NO_CLIENT
   if (*m_pcClient) {
      lwsl_notice("Client connecting to %s:%u....\n", m_pcAddress, m_iPort);
      /* we are in client mode */
      m_pWSI = lws_client_connect(pContext, m_pCAddress, iPort, (bool)iSecurePort, "/", m_pcAddress, "origin", NULL, -1);
      if (!m_pWSI) {
         lwsl_err("Client failed to connect to %s:%u\n", m_pcAddress, m_iPort);
         goto bail;
      }
      lwsl_notice("Client connected to %s:%u\n", m_pcAddress, m_iPort);
   }
#endif
   signal(SIGINT, sighandler);
   
   while (n >= 0 && !force_exit && (duration == 0 || duration > (time(NULL) -
                                                                 starttime))) {
#ifndef LWS_NO_CLIENT
      if (*pcClient) {
         struct timeval tv;
         gettimeofday(&tv, NULL);
         if (((unsigned int) tv.tv_usec - m_uiOldUs) > (unsigned int) m_iRateUs) {
            lws_callback_on_writable_all_protocol(&protocols[0]);
            uiOldUs = tv.tv_usec;
         }
      }
#endif
      if (new_pck_chk) {
         std::map <int, bool>::iterator i;
         i = packs_to_send.begin();
         while (i != packs_to_send.end()) {
            if (i->second) {
               std::list<lws*>& wa = *(path_wsi_map)[i->first];
               if (&wa != NULL) {
                  std::list<lws*>::iterator j = wa.begin();
                  while (j != wa.end()) {
                     lws_callback_on_writable(*j);
                     ffl_debug(FPL_WSSERV, "%s:%p",
                               id_path_map[i->first].c_str(), *j);
                     j++;
                  }
               } else {
                  break;
               }
            };
            i->second = false;
            i++;
         }
         new_pck_chk = false;
      }
      n = lws_service(m_pContext, 10);
      if(m_pSecureContext){
         N = lws_service(m_pSecureContext,10);
         if(N<0)break;
      }
   }
   force_exit = 1;
#ifndef LWS_NO_CLIENT
bail:
#endif
   lws_context_destroy(m_pContext);
   lws_context_destroy(m_pSecureContext);
   lwsl_notice("ferryfair exited cleanly\n");
#ifdef WIN32
#else
   closelog();
#endif
}

//string WSServer::per_session_data__http::vhost();
