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
#include <sys/stat.h>

#ifdef CMAKE_BUILD
#include "lws_config.h"
#endif

#include "JPEGImage.h"
#include "libwebsockets/lib/libwebsockets.h"
#include "WSServer.h"
#include "FFJSON.h"
#include "mystdlib.h"
#include "FerryStream.hpp"
#include "global.h"
#include <iostream>
#include <malloc.h>


static std::map<libwebsocket*, std::string>* wsi_path_map_l;
static std::map<std::string, std::list<libwebsocket*>*> *path_wsi_map_l;
static std::map<string, FerryStream*>* ferryStreams_l;

bool validate_path_l(std::string& p) {
    FFJSON::trimWhites(p);
    if (p.length() > 0) {
        return true;
    }
    return false;
}

static void
dump_handshake_info(struct libwebsocket *wsi) {
    int n;
    static const char *token_names[] = {
        /*[WSI_TOKEN_GET_URI]		=*/ "GET URI",
        /*[WSI_TOKEN_POST_URI]		=*/ "POST URI",
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

        fprintf(stderr, "    %s = %s\n", token_names[n], buf);
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

    if (!strcmp(&file[n - 5], ".html"))
        return "text/html";

    return NULL;
}

int WSServer::callback_http(struct libwebsocket_context *context,
        struct libwebsocket *wsi,
        enum libwebsocket_callback_reasons reason, void *user,
        void *in, size_t len) {
#if 0
    char client_name[128];
    char client_ip[128];
#endif
    char buf[256];
    char leaf_path[1024];
    char b64[64];
    struct timeval tv;
    int n, m;
    unsigned char *p;
    char *other_headers;
    static unsigned char buffer[4096];
    struct stat stat_buf;
    struct per_session_data__http *pss =
            (struct per_session_data__http *) user;
    const char *mimetype;
#ifdef EXTERNAL_POLL
    struct libwebsocket_pollargs *pa = (struct libwebsocket_pollargs *) in;
#endif

    switch (reason) {
        case LWS_CALLBACK_HTTP:

            dump_handshake_info(wsi);

            if (len < 1) {
                libwebsockets_return_http_status(context, wsi,
                        HTTP_STATUS_BAD_REQUEST, NULL);
                return -1;
            }

            /* this server has no concept of directories */
            if (strchr((const char *) in + 1, '/')) {
                libwebsockets_return_http_status(context, wsi,
                        HTTP_STATUS_FORBIDDEN, NULL);
                return -1;
            }

            /* if a legal POST URL, let it continue and accept data */
            if (lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI))
                return 0;

            /* check for the "send a big file by hand" example case */

            if (!strcmp((const char *) in, "/leaf.jpg")) {
                if (strlen(resource_path) > sizeof (leaf_path) - 10)
                    return -1;
                sprintf(leaf_path, "%s/leaf.jpg", resource_path);

                /* well, let's demonstrate how to send the hard way */

                p = buffer;

#ifdef WIN32
                pss->fd = open(leaf_path, O_RDONLY | _O_BINARY);
#else
                pss->fd = open(leaf_path, O_RDONLY);
#endif

                if (pss->fd < 0)
                    return -1;

                fstat(pss->fd, &stat_buf);

                /*
                 * we will send a big jpeg file, but it could be
                 * anything.  Set the Content-Type: appropriately
                 * so the browser knows what to do with it.
                 */

                p += sprintf((char *) p,
                        "HTTP/1.0 200 OK\x0d\x0a"
                        "Server: libwebsockets\x0d\x0a"
                        "Content-Type: image/jpeg\x0d\x0a"
                        "Content-Length: %u\x0d\x0a\x0d\x0a",
                        (unsigned int) stat_buf.st_size);

                /*
                 * send the http headers...
                 * this won't block since it's the first payload sent
                 * on the connection since it was established
                 * (too small for partial)
                 */

                n = libwebsocket_write(wsi, buffer,
                        p - buffer, LWS_WRITE_HTTP);

                if (n < 0) {
                    close(pss->fd);
                    return -1;
                }
                /*
                 * book us a LWS_CALLBACK_HTTP_WRITEABLE callback
                 */
                libwebsocket_callback_on_writable(context, wsi);
                break;
            }

            /* if not, send a file the easy way */
            strcpy(buf, resource_path);
            if (strcmp((const char*) in, "/")) {
                if (*((const char *) in) != '/')
                    strcat(buf, "/");
                strncat((char*) buf, (const char*) in, sizeof (buf) - strlen(resource_path));
            } else /* default file to serve */
                strcat(buf, "/test.html");
            buf[sizeof (buf) - 1] = '\0';

            /* refuse to serve files we don't understand */
            mimetype = get_mimetype(buf);
            if (!mimetype) {
                lwsl_err("Unknown mimetype for %s\n", buf);
                libwebsockets_return_http_status(context, wsi,
                        HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE, NULL);
                return -1;
            }

            /* demostrates how to set a cookie on / */

            other_headers = NULL;
            if (!strcmp((const char *) in, "/") &&
                    !lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_COOKIE)) {
                /* this isn't very unguessable but it'll do for us */
                gettimeofday(&tv, NULL);
                sprintf(b64, "LWS_%u_%u_COOKIE",
                        (unsigned int) tv.tv_sec,
                        (unsigned int) tv.tv_usec);

                sprintf(leaf_path,
                        "Set-Cookie: test=LWS_%u_%u_COOKIE;Max-Age=360000\x0d\x0a",
                        (unsigned int) tv.tv_sec, (unsigned int) tv.tv_usec);
                other_headers = leaf_path;
                lwsl_err(other_headers);
            }

            if (libwebsockets_serve_http_file(context, wsi, buf,
                    mimetype, other_headers))
                return -1; /* through completion or error, close the socket */

            /*
             * notice that the sending of the file completes asynchronously,
             * we'll get a LWS_CALLBACK_HTTP_FILE_COMPLETION callback when
             * it's done
             */

            break;

        case LWS_CALLBACK_HTTP_BODY:
            strncpy((char*) buf, (const char*) in, 20);
            buf[20] = '\0';
            if (len < 20)
                buf[len] = '\0';

            lwsl_notice("LWS_CALLBACK_HTTP_BODY: %s... len %d\n",
                    (const char *) buf, (int) len);

            break;

        case LWS_CALLBACK_HTTP_BODY_COMPLETION:
            lwsl_notice("LWS_CALLBACK_HTTP_BODY_COMPLETION\n");
            /* the whole of the sent body arried, close the connection */
            libwebsockets_return_http_status(context, wsi,
                    HTTP_STATUS_OK, NULL);

            return -1;

        case LWS_CALLBACK_HTTP_FILE_COMPLETION:
            //		lwsl_info("LWS_CALLBACK_HTTP_FILE_COMPLETION seen\n");
            /* kill the connection after we sent one file */
            return -1;

        case LWS_CALLBACK_HTTP_WRITEABLE:
            /*
             * we can send more of whatever it is we were sending
             */

            do {
                n = read(pss->fd, buffer, sizeof buffer);
                /* problem reading, close conn */
                if (n < 0)
                    goto bail;
                /* sent it all, close conn */
                if (n == 0)
                    goto flush_bail;
                /*
                 * because it's HTTP and not websocket, don't need to take
                 * care about pre and postamble
                 */
                m = libwebsocket_write(wsi, buffer, n, LWS_WRITE_HTTP);
                if (m < 0)
                    /* write failed, close conn */
                    goto bail;
                if (m != n)
                    /* partial write, adjust */
                    lseek(pss->fd, m - n, SEEK_CUR);

                if (m) /* while still active, extend timeout */
                    libwebsocket_set_timeout(wsi,
                        PENDING_TIMEOUT_HTTP_CONTENT, 5);

            } while (!lws_send_pipe_choked(wsi));
            libwebsocket_callback_on_writable(context, wsi);
            break;
flush_bail:
            /* true if still partial pending */
            if (lws_send_pipe_choked(wsi)) {
                libwebsocket_callback_on_writable(context, wsi);
                break;
            }

bail:
            close(pss->fd);
            return -1;

            /*
             * callback for confirming to continue with client IP appear in
             * protocol 0 callback since no websocket protocol has been agreed
             * yet.  You can just ignore this if you won't filter on client IP
             * since the default uhandled callback return is 0 meaning let the
             * connection continue.
             */

        case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
#if 0
            libwebsockets_get_peer_addresses(context, wsi, (int) (long) in, client_name,
                    sizeof (client_name), client_ip, sizeof (client_ip));

            fprintf(stderr, "Received network connect from %s (%s)\n",
                    client_name, client_ip);
#endif
            /* if we returned non-zero from here, we kill the connection */
            break;

#ifdef EXTERNAL_POLL
            /*
             * callbacks for managing the external poll() array appear in
             * protocol 0 callback
             */

        case LWS_CALLBACK_LOCK_POLL:
            /*
             * lock mutex to protect pollfd state
             * called before any other POLL related callback
             */
            break;

        case LWS_CALLBACK_UNLOCK_POLL:
            /*
             * unlock mutex to protect pollfd state when
             * called after any other POLL related callback
             */
            break;

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

        default:
            break;
    }

    return 0;
}

int WSServer::callbackFairPlayWS(struct libwebsocket_context *context,
        struct libwebsocket *wsi,
        enum libwebsocket_callback_reasons reason,
        void *user, void *in, size_t len) {
    struct per_session_data__fairplay *pss = (struct per_session_data__fairplay *) user;
    int n;
    std::list<FFJSON>::iterator ii;
    switch (reason) {

#ifndef LWS_NO_SERVER
            /* when the callback is used for server operations --> */

        case LWS_CALLBACK_SERVER_WRITEABLE:
            switch (pss->state) {
                case FRAGSTATE_INIT_PCK:
                    break;
                case FRAGSTATE_NEW_PCK:
                    break;
                case FRAGSTATE_MORE_FRAGS:
                    break;
                default:
                    break;
            }
            if (pss->morechunks) {
                std::string* targetString = pss->initpckflag ? pss->initpck : &pss->i->ffjson;
                int slength = ((int) pss->initByte - (int) targetString->c_str());
                int length = (targetString->length() - slength);
                int rlength = length - MAX_ECHO_PAYLOAD;
                length = length > MAX_ECHO_PAYLOAD ? MAX_ECHO_PAYLOAD : length;
                if (length <= 0) {
                    n = 4;
                    pss->initByte == NULL;
                    pss->morechunks = false;
                    pss->endHit = pss->initpckflag ? false : true;
                    pss->sum = 0;
                    pss->initpckflag = false;
                    //delete pss->initpck;
                } else {
                    n = libwebsocket_write(wsi, pss->initByte, length, (libwebsocket_write_protocol) (rlength > 0 ? (LWS_WRITE_CONTINUATION | LWS_WRITE_NO_FIN) : LWS_WRITE_CONTINUATION));
                    pss->initByte += length;
                    length = slength + length;
                    for (int i = slength; i < length; i++)
                        pss->sum += targetString->c_str()[i];
                }
                if (n < 0) {
                    lwsl_err("ERROR %d writing to socket, hanging up\n", n);
                    return 1;
                }
                if (n < (int) pss->len) {
                    lwsl_err("Partial write\n");
                    return -1;
                }
                if (rlength <= 0) {
                    ii = pss->i;
                    ii++;
                    if (ii == pss->fs->packetBuffer.end()) {
                        pss->endHit = true;
                    } else {
                        pss->i++;
                        libwebsocket_callback_on_writable(context, wsi);
                    }
                    pss->initByte == NULL;
                    pss->morechunks = false;
                    pss->initpckflag = false;
                } else {
                    libwebsocket_callback_on_writable(context, wsi);
                }
                break;
            }
            if (pss->initpckflag) {
                if (!pss->initiated) {
                    (*path_wsi_map_l)[pss->fs->path]->push_back(wsi);
                    pss->initiated = true;
                }
                size_t s;
                char * b64_s = base64_encode((const unsigned char*) JPEGImage::StdHuffmanTable, 420, (size_t*) & s);
                pss->initpck->append("{\"HuffmanTable\":\"");
                pss->initpck->append((const char*) b64_s, s);
                pss->initpck->append("\"}");
                free(b64_s);
                if (pss->initpck->length() < MAX_ECHO_PAYLOAD) {
                    n = libwebsocket_write(wsi, (unsigned char*) pss->initpck->c_str(), pss->initpck->length(), LWS_WRITE_TEXT);
                    pss->initpckflag = false;
                } else {
                    pss->initByte = (unsigned char*) pss->initpck->c_str();
                    n = libwebsocket_write(wsi, pss->initByte, MAX_ECHO_PAYLOAD, (libwebsocket_write_protocol) (LWS_WRITE_TEXT | LWS_WRITE_NO_FIN));
                    for (n = 0; n < MAX_ECHO_PAYLOAD; n++)
                        pss->sum += pss->initpck->c_str()[n];
                    pss->initByte += MAX_ECHO_PAYLOAD;
                    pss->morechunks = true;
                }
            } else {
                ii = pss->i;
                ii++;
                if (pss->endHit) {
                    pss->endHit = false;
                    if (ii != pss->fs->packetBuffer.end()) {
                        pss->i++;
                        ii++;
                    } else {
                        ffl_debug(FPL_WSSERV, "unexpected callback for frame %d on ws %p with path %s", (int) (*pss->i)["index"].val.number, wsi, pss->fs->path.c_str());
                        pss->endHit = true;
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
                ffl_debug(FPL_WSSERV, "frame index %d", (int) (*pss->i)["index"].val.number);
            }
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
            if (!pss->initpckflag) {
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
            } else {
                libwebsocket_callback_on_writable(context, wsi);
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
                int bufferSize;
                lwsl_notice("client has buffer size %d", bufferSize);
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
                            pss->fs = fs;
                            pss->initpckflag = true;
                            pss->initpck = new std::string();
                            bufferSize = (int) ffjson["bufferSize"].val.number;
                            bufferSize = (bufferSize > 0)&&(pss->fs->packetBufferSize >= bufferSize) ? bufferSize : pss->fs->packetBufferSize; //validate input data
                            int initpck = (initpck = (fs->packetBuffer.size() - bufferSize)) > 0 ? initpck : 0;
                            pss->i = fs->packetBuffer.begin();
                            std::advance(pss->i, initpck);
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
    //memset(this->protocols, 0, 2 * sizeof (struct libwebsocket_protocols));
    //    this->protocols[0].callback = &WSServer::callback_echo;
    //    this->protocols[0].name = "default";
    //    this->protocols[0].per_session_data_size = sizeof (struct per_session_data__fairplay);
    //    this->protocols[1].name = NULL;
    //    this->protocols[1].callback = NULL;
    //    this->protocols[1].per_session_data_size = 0;
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
                            ffl_debug(FPL_WSSERV, "%s:%d", i->first.c_str(), *j);
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