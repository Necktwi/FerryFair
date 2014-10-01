/* 
 * File:   FerryStream.h
 * Author: Gowtham
 *
 * Created on September 28, 2014, 5:01 PM
 */

#ifndef FERRYSTREAM_H
#define	FERRYSTREAM_H

#include "global.h"
#include <base/ServerSocket.h>
#include <base/SocketException.h>
#include <base/mystdlib.h>
#include <base/myconverters.h>
#include <base/FFJSON.h>
#include <base/JPEGImage.h>
#include <base/logger.h>
#include <cstdlib>
#include <string>
#include <iostream>
#include <fstream>
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

class FerryStream {
public:

    class Exception : std::exception {
    public:

        Exception(std::string e);

        const char* what() const throw ();

        ~Exception() throw ();

    private:
        std::string identifier;
    };
    std::string path;

    FerryStream();

    FerryStream(const FerryStream& orig);

    FerryStream(ServerSocket::Connection * source, void(*funeral)(string));

    ~FerryStream();

    short packetBufferSize = 60;
    list<FFJSON*> packetBuffer;
    map<int, std::string> packetStrBuffer;

    void die();

    bool isConnectionAlive();

private:
    ServerSocket::Connection * source = NULL;
    thread* heartThread;
    bool suicide = false;
    string buffer = "";

    static void heart(FerryStream* fs);
    void(*funeral)(string path);
};

#endif	/* FERRYSTREAM_H */

