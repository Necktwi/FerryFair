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

using namespace std;

/*
 * 
 */

int port = 0;

class FerryStream {
public:

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
    std::string path;

    FerryStream() {
    }

    FerryStream(ServerSocket::Connection * source, void(*funeral)(string)) : source(source), funeral(funeral) {
        string initPack;
        try {
            *source >> initPack;
        } catch (SocketException e) {
            throw Exception("Unable to receive a initial packet.");
        }
        FFJSON* init_ffjson;
        try {
            init_ffjson = new FFJSON(initPack);
        } catch (FFJSON::Exception e) {
            throw Exception("Unable parse initial packet. FFJSON::Exception:" + string(e.what()));
        }
        this->path = string((*init_ffjson)["path"].val.string);
        source->MAXRECV = (*init_ffjson)["MAXPACKSIZE"].val.number;
        if ((debug & 1) == 1) {
            cout << "\n" << getTime() << " FerryStream: new connection received with path :" << this->path << "\n";
            fflush(stdout);
        }
        delete init_ffjson;
        this->heartThread = new thread(&FerryStream::heart, this);
        //pthread_create(&heartThread, NULL, (void*(*)(void*)) & this->heart, NULL);
    }

    ~FerryStream() {
        delete this->source;
    }

    void die() {
        suicide = true;
        delete source;
    }

    bool isConnectionAlive() {
        string testPacket = "{\"test\"}";
        try {
            *this->source << testPacket;
        } catch (SocketException e) {
            return false;
        }
        return true;
    }

private:
    ServerSocket::Connection * source = NULL;
    thread* heartThread;
    bool suicide = false;

    static void heart(FerryStream* fs) {
        string buffer = "";
        string truebuffer = "";
        bool pckEndReached = false;
        bool goodPacket = true;
        string index_str = "";
        int index = 0;
        int dindex = 7;
        while (true) {
            pckEndReached = false;
            try {
                while (!pckEndReached) {
                    *fs->source >> buffer;
                    if ((int) buffer.find("{index:", 0) == 0) {
                        if (!goodPacket) {
                            if ((debug & 1) == 1) {
                                cout << "\n" << getTime() << " heart: packet corrupted.\n";
                            }
                        }
                        truebuffer = buffer;
                        dindex = 7;
                        while (buffer[dindex] != ',' && dindex < 28) {
                            index_str += buffer[dindex];
                            dindex++;
                        }
                        index = atof(index_str.c_str());
                    } else {
                        truebuffer += buffer;
                    }
                    if (atoi(truebuffer.substr(truebuffer.length() - 1 - index_str.length(), index_str.length()).c_str()) == index) {
                        pckEndReached = true;
                        goodPacket = true;
                    }
                }
                FFJSON media_pack(buffer);
                if ((*media_pack.val.pairs)["ferryframes"] != NULL) {
                    vector<FFJSON*>* frames = media_pack["ferryframes"].val.array;
                    int i = frames->size();
                    char* frame;
                    string fn_b = media_pack["index"].ffjson;
                    string fn;
                    int fd;
                    while (i > 0) {
                        frame = (*frames)[i]->val.string;
                        fn = fn_b + string(itoa(i));
                        fd = open(fn.c_str(), O_WRONLY | O_TRUNC | O_CREAT);
                        write(fd, frame, (*frames)[i]->length);
                        close(fd);
                        i--;
                    }
                }
            } catch (SocketException e) {
                if ((debug & 1) == 1) {
                    cout << "\n" << getTime() << " FerryStream::heart: I'm " << fs->path << " dying! Connection to " << fs->source->getDestinationIP() << "\n";
                    fflush(stdout);
                }
                delete fs->source;
                fs->funeral(fs-> path);
                break;
            }

        }
    }
    void(*funeral)(string path);
};
std::map<string, FerryStream*> ferrystreams;

void ferryStreamFuneral(string stream_path) {
    //delete ferrystreams[stream_path];
    ferrystreams[stream_path] = NULL;
}

int main(int argc, char** argv) {
    port = 92711;
    ServerSocket* ss;
    try {
        ss = new ServerSocket(92711);
    } catch (SocketException e) {
        cout << "\n" << getTime() << " ferrymediaserver: Unable to create socket on port: " << port << ".\n";
    }
    debug = 1;
    while (true) {
        try {
            if ((debug & 1) == 1) {
                cout << "\n" << getTime() << " ferrymediaserver: waiting for a connection.\n";
                fflush(stdout);
            }
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
            if ((debug & 1) == 1) {
                cout << "\n" << getTime() << " ferrymediaserver: a connection accepted.\n";
                fflush(stdout);
            }
        } catch (SocketException e) {
            if ((debug & 1) == 1) {
                cout << "\n" << getTime() << " ferrymediaserver: Exception accepting incoming connection: " << e.description() << "\n";
                fflush(stdout);
            }
        } catch (FerryStream::Exception e) {
            cout << "\n" << getTime() << " ferrymediaserver: Exception creating a new FerrySteam: " << e.what() << "\n";
            fflush(stdout);
        }
    }
    delete ss;
    return 0;
}

