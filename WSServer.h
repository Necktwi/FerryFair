/* 
 * File:   WSServer.h
 * Author: Gowtham
 *
 * Created on December 12, 2013, 6:34 PM
 */

#ifndef WSSERVER_H
#define	WSSERVER_H

#include "libwebsockets/lib/libwebsockets.h"
#include "FerryStream.hpp"
#include <string>
#include <map>

class WSServer {
public:
    struct libwebsocket_protocols protocols[2];
    bool daemonize;
    int client;
    int rate_us;
    int debug_level;
    int use_ssl;
    int port;
    char interface[128];
    int syslog_options;
    int listen_port;
    struct lws_context_creation_info info;
    int opts = 0;
    struct libwebsocket_context *context;
#ifndef LWS_NO_CLIENT
    char address[256];
    unsigned int oldus = 0;
    struct libwebsocket *wsi;
#endif

    class Exception : std::exception {
    public:

        Exception(std::string e) : identifier(e) {

        }

        const char* what() const throw () {
            return this->identifier.c_str();
        }

        ~Exception() throw () {
        }
    private:
        std::string identifier;
    };

    struct WSServerArgs {
        bool daemonize;
        char client[20];
        int rate_us;
        int debug_level;
        int use_ssl;
        int port;
        char intreface[20];
        std::map<libwebsocket*, std::string>* wsi_path_map;
        std::map<std::string, std::list<libwebsocket*>*> *path_wsi_map;
        std::map<std::string, FerryStream*>* ferryStreams;
    };
    WSServer(WSServerArgs* args);
    int static callback_echo(struct libwebsocket_context *context, struct libwebsocket *wsi, enum libwebsocket_callback_reasons reason, void *user, void *in, size_t len);
private:
    static int heart(WSServer* wss);
    thread* heartThread;
};
#endif	/* WSSERVER_H */
