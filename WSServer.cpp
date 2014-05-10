/*
 * libwebsockets-test-echo - libwebsockets echo test implementation
 *
 * This implements both the client and server sides.  It defaults to
 * serving, use --client <remote address> to connect as client.
 *
 * Copyright (C) 2010-2013 Andy Green <andy@warmcat.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation:
 *  version 2.1 of the License.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301  USA
 */

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

#ifdef CMAKE_BUILD
#include "lws_config.h"
#endif

#include "libwebsockets/lib/libwebsockets.h"
#include "WSServer.h"
#include "FFJSON.h"
#include "mystdlib.h"
#include "FerryStream.hpp"
#include "global.h"
#include <iostream>



#define MAX_ECHO_PAYLOAD 8000
#define LOCAL_RESOURCE_PATH "./WebsocketServer"


static std::map<libwebsocket*, std::string>* wsi_path_map_l;
static std::map<std::string, std::list<libwebsocket*>*> *path_wsi_map_l;
static std::map<string, FerryStream*>* ferryStreams_l;

struct per_session_data__echo {
    unsigned char buf[LWS_SEND_BUFFER_PRE_PADDING + MAX_ECHO_PAYLOAD + LWS_SEND_BUFFER_POST_PADDING];
    unsigned int len;
    unsigned int index;
    bool initiated;
    std::list<FFJSON>::iterator i;
    FerryStream* fs;
    bool endHit;
    bool morechunks;
    unsigned char* initByte;
    unsigned long sum;
};

bool validate_path_l(std::string& p) {
    FFJSON::trimWhites(p);
    if (p.length() > 0) {
        return true;
    }
    return false;
}

int WSServer::callback_echo(struct libwebsocket_context *context, struct libwebsocket *wsi, enum libwebsocket_callback_reasons reason, void *user, void *in, size_t len) {
    struct per_session_data__echo *pss = (struct per_session_data__echo *) user;
    int n;
    std::list<FFJSON>::iterator ii;
    switch (reason) {

#ifndef LWS_NO_SERVER
            /* when the callback is used for server operations --> */

        case LWS_CALLBACK_SERVER_WRITEABLE:
            if (pss->morechunks) {
                int slength = ((int) pss->initByte - (int) pss->i->ffjson.c_str());
                int length = (pss->i->ffjson.length() - slength);
                int rlength = length - MAX_ECHO_PAYLOAD;
                length = length > MAX_ECHO_PAYLOAD ? MAX_ECHO_PAYLOAD : length;
                if (length <= 0) {
                    //                    unsigned char bp[4];
                    //                    bp[0] = pss->sum >> 24;
                    //                    bp[1] = pss->sum >> 16;
                    //                    bp[2] = pss->sum >> 8;
                    //                    bp[3] = pss->sum;
                    //                    n = libwebsocket_write(wsi, bp, 4, LWS_WRITE_BINARY);
                    n = 4;
                    pss->initByte == NULL;
                    pss->morechunks = false;
                    pss->endHit = true;
                    pss->sum = 0;
                } else {
                    n = libwebsocket_write(wsi, pss->initByte, length, (libwebsocket_write_protocol) (rlength > 0 ? (LWS_WRITE_CONTINUATION | LWS_WRITE_NO_FIN) : LWS_WRITE_CONTINUATION));
                    pss->initByte += length;
                    length = slength + length;
                    for (int i = slength; i < length; i++)
                        pss->sum += pss->i->ffjson.c_str()[i];
                }
                if (n < 0) {
                    lwsl_err("ERROR %d writing to socket, hanging up\n", n);
                    return 1;
                }
                if (n < (int) pss->len) {
                    lwsl_err("Partial write\n");
                    return -1;
                }
                libwebsocket_callback_on_writable(context, wsi);
                break;
            }
            ii = pss->i;
            ii++;
            if (pss->endHit) {
                pss->endHit = false;
                if (ii != pss->fs->packetBuffer.end()) {
                    pss->i++;
                    ii++;
                } else {
                    lwsl_warn("unexpected callback for frame %d on ws %p with path %s", (int) (*pss->i)["index"].val.number, wsi, pss->fs->path.c_str());
                    pss->endHit = true;
                    if (!pss->initiated) {
                        (*path_wsi_map_l)[pss->fs->path]->push_back(wsi);
                        pss->initiated = true;
                    }
                    break;
                }
            }
            if (pss->i->ffjson.length() < MAX_ECHO_PAYLOAD) {
                n = libwebsocket_write(wsi, (unsigned char*) pss->i->ffjson.c_str(), pss->i->ffjson.length(), LWS_WRITE_TEXT);
            } else {
                pss->initByte = (unsigned char*) pss->i->ffjson.c_str();
                n = libwebsocket_write(wsi, pss->initByte, MAX_ECHO_PAYLOAD, (libwebsocket_write_protocol) (LWS_WRITE_TEXT | LWS_WRITE_NO_FIN));
                for (n = 0; n < MAX_ECHO_PAYLOAD; n++)
                    pss->sum += pss->i->ffjson.c_str()[n];
                pss->initByte += MAX_ECHO_PAYLOAD;
                pss->morechunks = true;
            }
            lwsl_notice("frame index %d", (int) (*pss->i)["index"].val.number);
            if (n < 0) {
                lwsl_err("ERROR %d writing to socket, hanging up\n", n);
                return 1;
            }
            if (n < (int) pss->len) {
                lwsl_err("Partial write\n");
                return -1;
            }
            if (pss->morechunks) {
                libwebsocket_callback_on_writable(context, wsi);
                break;
            }
            if (ii != pss->fs->packetBuffer.end()) {
                pss->i++;
                libwebsocket_callback_on_writable(context, wsi);
            } else {
                pss->endHit = true;
                if (!pss->initiated) {
                    (*path_wsi_map_l)[pss->fs->path]->push_back(wsi);
                    pss->initiated = true;
                }
            }
            break;

        case LWS_CALLBACK_RECEIVE:
            if (len > MAX_ECHO_PAYLOAD) {
                lwsl_err("Server received packet bigger than %u, hanging up\n", MAX_ECHO_PAYLOAD);
                return 1;
            }
            //            memcpy(&pss->buf[LWS_SEND_BUFFER_PRE_PADDING], in, len);
            //            pss->len = (unsigned int) len;
            //            libwebsocket_callback_on_writable(context, wsi);
            try {
                std::string str_in = std::string((char*) in);
                FFJSON ffjson(str_in);
                std::string path = std::string((char*) (ffjson[std::string("path")].val.string), ffjson[std::string("path")].length);
                std::string initPack;
                int bufferSize = (int) ffjson["bufferSize"].val.number;
                if (validate_path_l(path)) {
                    if ((*wsi_path_map_l)[wsi].length() != 0) {
                        if ((*wsi_path_map_l)[wsi].compare(path) == 0) {
                            //ignore
                        }
                    } else {
                        (*wsi_path_map_l)[wsi] = path;
                        if ((*path_wsi_map_l)[path] == NULL) {
                            (*path_wsi_map_l)[path] = new std::list<libwebsocket*>();
                        }
                        FerryStream* fs = (*ferryStreams_l)[path];
                        if (fs != NULL) {
                            pss->i = fs->packetBuffer.begin();
                            pss->fs = fs;
                            //                            initPack = "{\"initialPacks\":" + std::to_string(fs->packetBuffer.size()) + "}";
                            //                            libwebsocket_write(wsi, (unsigned char*) initPack.c_str(), initPack.length(), LWS_WRITE_TEXT);
                            if (fs->packetBuffer.size() > 0) {
                                libwebsocket_callback_on_writable(context, wsi);
                                //                                while (i != fs->packetBuffer.end()) {
                                //                                    pss->payload = &i->ffjson;
                                //                                    libwebsocket_callback_on_writable(context, wsi);
                                //                                    if ((debug & 2) == 2) {
                                //                                        std::cout << "\n" << getTime() << " callback_echo: " << path << ":" << (*i)["index"].val.number << " is qued to " << wsi << "\n";
                                //                                    }
                                //                                    //libwebsocket_write(wsi, (unsigned char*) i->ffjson.c_str(), i->ffjson.length(), LWS_WRITE_TEXT);
                                //                                    i++;
                                //                                }
                            }
                        } else {
                            //initPack = "{\"error\":\"No stream yet started on this path\"}";
                            //libwebsocket_write(wsi, (unsigned char*) initPack.c_str(), initPack.length(), LWS_WRITE_TEXT);
                        }
                    }
                } else {
                    char response[] = "{\"error\":\"Illegal path\"}";
                    memcpy(&pss->buf[LWS_SEND_BUFFER_PRE_PADDING], response, strlen(response));
                    libwebsocket_callback_on_writable(context, wsi);
                }
            } catch (FFJSON::Exception e) {
                std::cout << "\n" << getTime() << " LWS_CALLBACK_RECEIVE: incomprehensible data received" << " \n";
                char response[] = "{\"error\":\"data isn't in JSON format\"}";
                memcpy(&pss->buf[LWS_SEND_BUFFER_PRE_PADDING], response, strlen(response));
                libwebsocket_callback_on_writable(context, wsi);
                break;
            }
            break;

        case LWS_CALLBACK_CLOSED:
        {
            std::map<libwebsocket*, std::string>::iterator i;
            i = wsi_path_map_l->begin();
            std::string path;
            while (i != wsi_path_map_l->end()) {
                if (wsi == i->first) {
                    path = i->second;
                    wsi_path_map_l->erase(i);
                    break;
                }
            }
            if (path.length() != 0) {
                std::list<libwebsocket*>& wsil = *(*path_wsi_map_l)[path];
                std::list<libwebsocket*>::iterator j = wsil.begin();
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
            n = libwebsocket_write(wsi, &pss->buf[LWS_SEND_BUFFER_PRE_PADDING], pss->len, LWS_WRITE_TEXT);
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

void sighandler(int sig) {
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

WSServer::WSServer(WSServerArgs* args) {
    ferryStreams_l = args->ferryStreams;
    path_wsi_map_l = args->path_wsi_map;
    wsi_path_map_l = args->wsi_path_map;

    int n = 0;
    port = 17291;
    use_ssl = 0;
    char interface_name[128] = "";
#ifndef WIN32
    syslog_options = LOG_PID | LOG_PERROR;
#endif

#ifndef LWS_NO_CLIENT
    rate_us = 250000;
#endif

#ifndef LWS_NO_DAEMONIZE
    daemonize = 0;
#endif

    memset(&info, 0, sizeof info);

#ifndef LWS_NO_CLIENT
    lwsl_notice("Built to support client operations\n");
#endif
#ifndef LWS_NO_SERVER
    lwsl_notice("Built to support server operations\n");
#endif

#ifndef LWS_NO_CLIENT
#endif

#ifndef LWS_NO_DAEMONIZE
    if (args->daemonize) {
        daemonize = 1;
#ifndef WIN32
        syslog_options &= ~LOG_PERROR;
#endif
    }
#endif
#ifndef LWS_NO_CLIENT
    if (strlen(args->client) > 0) {
        client = 1;
        strcpy(address, args->client);
        port = 80;
    }
    if (args->rate_us != 0) {
        rate_us = args->rate_us * 1000;
    }
#endif
    debug_level = args->debug_level;
    use_ssl = args->use_ssl; /* 1 = take care about cert verification, 2 = allow anything */
    if (args->port > 0) {
        port = args->port;
    }
    if (strlen(args->intreface) > 0) {
        strncpy(interface, args->intreface, sizeof interface);
        interface[(sizeof interface) - 1] = '\0';
    }
    memset(this->protocols, 0, 2 * sizeof (struct libwebsocket_protocols));
    this->protocols[0].callback = &WSServer::callback_echo;
    this->protocols[0].name = "default";
    this->protocols[0].per_session_data_size = sizeof (struct per_session_data__echo);
    this->protocols[1].name = NULL;
    this->protocols[1].callback = NULL;
    this->protocols[1].per_session_data_size = 0;
    this->heartThread = new thread(&WSServer::heart, this);
}

int WSServer::heart(WSServer* wss) {
    int n = 0;
    //                fprintf(stderr, "Usage: libwebsockets-test-echo "
    //                        "[--ssl] "
#ifndef LWS_NO_CLIENT
    //                        "[--client <remote ads>] "
    //                        "[--ratems <ms>] "
#endif
    //                        "[--port=<p>] "
    //                        "[-d <log bitfield>]\n");
    //                exit(1);

#ifndef LWS_NO_DAEMONIZE
    /*
     * normally lock path would be /var/lock/lwsts or similar, to
     * simplify getting started without having to take care about
     * permissions or running as root, set to /tmp/.lwsts-lock
     */
    if (!wss->client && wss->daemonize && lws_daemonize("/tmp/.lwstecho-lock")) {
        fprintf(stderr, "Failed to daemonize\n");
        return 1;
    }
#endif

#ifdef WIN32
#else
    /* we will only try to log things according to our debug_level */
    setlogmask(LOG_UPTO(LOG_DEBUG));
    openlog("lwsts", wss->syslog_options, LOG_DAEMON);

    /* tell the library what debug level to emit and to send it to syslog */
    lws_set_log_level(wss->debug_level, lwsl_emit_syslog);
#endif
    lwsl_notice("libwebsockets echo test - "
            "(C) Copyright 2010-2013 Andy Green <andy@warmcat.com> - "
            "licensed under LGPL2.1\n");
#ifndef LWS_NO_CLIENT
    if (wss->client) {
        lwsl_notice("Running in client mode\n");
        wss->listen_port = CONTEXT_PORT_NO_LISTEN;
        if (wss->use_ssl)
            wss->use_ssl = 2;
    } else {
#endif
#ifndef LWS_NO_SERVER
        lwsl_notice("Running in server mode\n");
        wss->listen_port = wss->port;
#endif
#ifndef LWS_NO_CLIENT
    }
#endif

    wss->info.port = wss->listen_port;
    wss->info.iface = strlen(wss->interface) > 0 ? wss->interface : NULL;
    wss->info.protocols = wss->protocols;
#ifndef LWS_NO_EXTENSIONS
    wss->info.extensions = libwebsocket_get_internal_extensions();
#endif
    if (wss->use_ssl && !wss->client) {
        wss->info.ssl_cert_filepath = LOCAL_RESOURCE_PATH"/libwebsockets-test-server.pem";
        wss->info.ssl_private_key_filepath = LOCAL_RESOURCE_PATH"/libwebsockets-test-server.key.pem";
    }
    wss->info.gid = -1;
    wss->info.uid = -1;
    wss->info.options = wss->opts;

    wss->context = libwebsocket_create_context(&wss->info);

    if (wss->context == NULL) {
        lwsl_err("libwebsocket init failed\n");
        return -1;
    }

#ifndef LWS_NO_CLIENT
    if (wss->client) {
        lwsl_notice("Client connecting to %s:%u....\n", wss->address, wss->port);
        /* we are in client mode */
        wss->wsi = libwebsocket_client_connect(wss->context, wss->address, wss->port, wss->use_ssl, "/", wss->address, "origin", NULL, -1);
        if (!wss->wsi) {
            lwsl_err("Client failed to connect to %s:%u\n", wss->address, wss->port);
            goto bail;
        }
        lwsl_notice("Client connected to %s:%u\n", wss->address, wss->port);
    }
#endif
    signal(SIGINT, sighandler);

    while (n >= 0 && !force_exit) {
#ifndef LWS_NO_CLIENT
        struct timeval tv;

        if (wss->client) {
            gettimeofday(&tv, NULL);

            if (((unsigned int) tv.tv_usec - wss->oldus) > (unsigned int) wss->rate_us) {
                libwebsocket_callback_on_writable_all_protocol(&wss->protocols[0]);
                wss->oldus = tv.tv_usec;
            }
        }
#endif
        if (new_pck_chk) {
            std::map < std::string, bool>::iterator i;
            i = packs_to_send.begin();
            while (i != packs_to_send.end()) {
                if (i->second) {
                    std::list<libwebsocket*>& wa = *(*path_wsi_map_l)[i->first];
                    if (&wa != NULL) {
                        std::list<libwebsocket*>::iterator j = wa.begin();
                        while (j != wa.end()) {
                            libwebsocket_callback_on_writable(wss->context, *j);
                            if ((debug & 2) == 2) {
                                std::cout << "\n" << getTime() << " callback_echo: " << i->first << ":" << *j << " qued \n";
                            }
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
        n = libwebsocket_service(wss->context, 10);
    }
#ifndef LWS_NO_CLIENT
bail:
#endif
    libwebsocket_context_destroy(wss->context);

    lwsl_notice("libwebsockets-test-echo exited cleanly\n");
#ifdef WIN32
#else
    closelog();
#endif
}