/* 
 * File:   main.cpp
 * Author: gowtham
 *
 * Created on 24 November, 2013, 12:25 AM
 */

#include "ServerSocket.h"
#include "SocketException.h"
#include "mystdlib.h"
#include "myconverters.h"
#include "debug.h"
#include "FFJSON.h"
#include "WSServer.h"
#include "libwebsockets/lib/libwebsockets.h"
#include "FerryStream.hpp"
#include "global.h"
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

using namespace std;

/*
 * 
 */

int port = 0;
std::map<libwebsocket*, string> wsi_path_map;
std::map < std::string, list < libwebsocket* >*> path_wsi_map;

std::map<string, FerryStream*> ferrystreams;

void ferryStreamFuneral(string stream_path) {
    //delete ferrystreams[stream_path];
    ferrystreams[stream_path] = NULL;
}

int main(int argc, char** argv) {
    port = 92711;
    ServerSocket* ss;
    debug = 1;

    WSServer::WSServerArgs ws_server_args;
    memset(&ws_server_args, 0, sizeof (WSServer::WSServerArgs));
    ws_server_args.path_wsi_map = &path_wsi_map;
    ws_server_args.wsi_path_map = &wsi_path_map;
    ws_server_args.debug_level = 31;
    ws_server_args.ferryStreams = &ferrystreams;
    WSServer* wss = new WSServer(&ws_server_args);
    try {
        ss = new ServerSocket(92711);
    } catch (SocketException e) {
        ffl_err("Unable to create socket on port: %d", port);
        exit(1);
    }
    while (true && !force_exit) {
        try {
            ffl_notice("waiting for a connection...");
            FerryStream* fs = new FerryStream(ss->accept(), &ferryStreamFuneral);
            if (ferrystreams[fs->path] == NULL) {
                ferrystreams[fs->path] = fs;
            } else {
                if (ferrystreams[fs->path]->isConnectionAlive()) {
                    delete fs;
                } else {
                    delete ferrystreams[fs->path];
                    ferrystreams[fs->path] = fs;
                }
            }
            ffl_notice("a connection accepted.");
        } catch (SocketException e) {
            ffl_warn("Exception accepting incoming connection: %s", e.description().c_str());
        } catch (FerryStream::Exception e) {
            ffl_err("Exception creating a new FerryStream: %s", e.what());
            fflush(stdout);
        }
    }
    delete ss;
    return 0;
}

