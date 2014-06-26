/* 
 * File:   WSServer.h
 * Author: Gowtham
 *
 * Created on December 12, 2013, 6:34 PM
 */

#ifndef WSSERVER_H
#define	WSSERVER_H

#define MAX_ECHO_PAYLOAD 8000
#define LOCAL_RESOURCE_PATH "./WebsocketServer"


#include "libwebsockets/lib/libwebsockets.h"
#include "FerryStream.hpp"
#include <string>
#include <map>

class WSServer {
public:

    enum FraggleStates {
        FRAGSTATE_INIT_PCK,
        FRAGSTATE_NEW_PCK,
        FRAGSTATE_MORE_FRAGS,
        FRAGSTATE_INCOMPREHENSIBLE_INIT_MSG,
    };

    enum Protocol {
        PROTOCOL_HTTP = 0,
        PROTOCOL_FFJSON
    };
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

    struct per_session_data__http {
        int fd;
    };

    struct per_session_data__fairplay {
        unsigned char buf[LWS_SEND_BUFFER_PRE_PADDING + MAX_ECHO_PAYLOAD + LWS_SEND_BUFFER_POST_PADDING];
        unsigned int len;
        unsigned int index;
        bool initiated;
        std::list<FFJSON>::iterator i;
        FerryStream* fs;

        /**
         * It is set if the packet in the list pointed by i is the last packet in the list when the last packet was sent. when sending a packet if it is set 'i' is incremented before sending the packet.
         */
        bool endHit;
        bool morechunks;

        /**
         * when there are more chunks to send the initByte is assigned with address pointing to next chunk.
         */
        unsigned char* initByte;
        unsigned long sum;

        /**
         * To flag that its the fist packet to be sent to the client.
         */
        bool initpckflag;
        std::string* initpck;

        /**
         * 
         */
        enum WSServer::FraggleStates state;
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
    libwebsocket_protocols protocols[2] = {
        /* first protocol must always be HTTP handler */
        //        {
        //            "http-only", /* name */
        //            WSServer::callback_http, /* callback */
        //            sizeof (struct per_session_data__http), /* per_session_data_size */
        //            0, /* max frame size / rx buffer */
        //        },
        {
            "fairplay",
            WSServer::callbackFairPlayWS,
            sizeof (struct per_session_data__fairplay),
            10,
        },
        { NULL, NULL, 0, 0} /* terminator */
    };

private:
    static int heart(WSServer* wss);
    int static callbackFairPlayWS(struct libwebsocket_context *context,
            struct libwebsocket *wsi,
            enum libwebsocket_callback_reasons reason,
            void *user, void *in, size_t len);
    static int callback_http(struct libwebsocket_context *context,
            struct libwebsocket *wsi,
            enum libwebsocket_callback_reasons reason, void *user,
            void *in, size_t len);
    thread* heartThread;
};
#endif	/* WSSERVER_H */
