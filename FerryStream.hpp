/* 
 * File:   FerryStream.hpp
 * Author: Gowtham
 *
 * Created on December 15, 2013, 2:55 PM
 */

#ifndef FERRYSTREAM_HPP
#define	FERRYSTREAM_HPP

#include "ServerSocket.h"
#include "SocketException.h"
#include "mystdlib.h"
#include "myconverters.h"
#include "debug.h"
#include "FFJSON.h"
#include "WSServer.h"
#include "libwebsockets/lib/libwebsockets.h"
#include "global.h"
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
            *source >> this->buffer;
        } catch (SocketException e) {
            throw Exception("Unable to receive a initial packet.");
        }
        initPack = this->buffer.substr(0, this->buffer.find('}') + 1);
        FFJSON* init_ffjson;
        try {
            init_ffjson = new FFJSON(initPack);
        } catch (FFJSON::Exception e) {
            throw Exception("Unable parse initial packet. FFJSON::Exception:" + string(e.what()));
        }
        try {
            if ((this->path = string((*init_ffjson)["path"].val.string)).length() < 0) {
                throw Exception("path length didn't met required value");
                delete init_ffjson;
            };
            if ((source->MAXRECV = (*init_ffjson)["MAXPACKSIZE"].val.number) <= 0) {
                throw Exception("Max packet size didn't met required value");
                delete init_ffjson;
            };
        } catch (FFJSON::Exception e) {
            throw Exception("Illegal initial packet. FFJSON::Exception:" + string(e.what()));
            delete init_ffjson;
        }
        if ((debug & 1) == 1) {
            cout << "\n" << getTime() << " FerryStream: new connection received with path :" << this->path << "\n";
            fflush(stdout);
        }
        delete init_ffjson;
        packs_to_send[this->path] = new list<FFJSON*>();
        this->heartThread = new thread(&FerryStream::heart, this);
        //pthread_create(&heartThread, NULL, (void*(*)(void*)) & this->heart, NULL);
    }

    ~FerryStream() {
        delete this->source;
        std::map < std::string, bool> ::iterator i;
        i = packs_to_send.begin();
        while (i != packs_to_send.end()) {
            if (i->first.compare(this->path) == 0) {
                packs_to_send.erase(i);
                break;
            }
            i++;
        }
    }

    short packetBufferSize = 60;
    list<FFJSON> packetBuffer;

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
    string buffer = "";

    static void heart(FerryStream* fs) {
        string truebuffer = "";
        bool pckEndReached = false;
        bool goodPacket = true;
        string index_str = "";
        int index = 0;
        int dindex = 7;
        string terminatingSign = "";
        int packStartIndex = -1;
        int packEndIndex = -1;
        string bridge;
        while (true && !force_exit) {
            pckEndReached = false;
            truebuffer = "";
            try {
                while (!pckEndReached && !force_exit) {
                    fs->buffer = bridge + fs->buffer;
                    if (terminatingSign.length() > 0 && (packEndIndex = fs->buffer.find(terminatingSign, 0)) >= 0) {
                        int elength = packEndIndex + terminatingSign.length() - bridge.length();
                        truebuffer += fs->buffer.substr(bridge.length(), elength);
                        pckEndReached = true;
                        goodPacket = true;
                        terminatingSign = "";
                        break;
                    }
                    if ((packStartIndex = (int) fs->buffer.find("{index:", packStartIndex)) >= 0) {
                        if (!goodPacket) {
                            if ((debug & 1) == 1) {
                                cout << "\n" << getTime() << fs->path << ": heart: packet " << index << " corrupted.\n";
                            }
                        }
                        goodPacket = false;
                        dindex = 7 + packStartIndex;
                        index_str = "";
                        int i = 0;
                        while (fs->buffer[dindex] != ',' && i < 28) {
                            index_str += fs->buffer[dindex];
                            dindex++;
                            i++;
                        }
                        index = (int) atof(index_str.c_str());
                        if (fs->buffer[dindex] == ',' && index > 0) {
                            terminatingSign = "endex:" + index_str + "}";
                            if (terminatingSign.length() > 0 && (packEndIndex = fs->buffer.find(terminatingSign, packStartIndex)) >= 0) {
                                int eindex = packEndIndex + terminatingSign.length();
                                truebuffer = fs->buffer.substr(packStartIndex, eindex);
                                packStartIndex += eindex + 1;
                                pckEndReached = true;
                                goodPacket = true;
                                terminatingSign = "";
                                break;
                            } else {
                                truebuffer = fs->buffer.substr(packStartIndex, fs->buffer.length());
                            }
                        } else {
                            packStartIndex += 9;
                        }
                    } else if (terminatingSign.length() > 0) {
                        truebuffer += fs->buffer.substr(bridge.length(), fs->buffer.length()); //truebuffer should be assigned prior to bridge
                    }
                    if (terminatingSign.length() > 0) {
                        bridge = fs->buffer.substr(fs->buffer.length() - terminatingSign.length() + 1, fs->buffer.length());
                    }

                    *fs->source >> fs->buffer;
                    packStartIndex = 0;
                }
                try {
                    FFJSON* media_pack = new FFJSON(truebuffer);
                    if (media_pack->type == FFJSON::FFJSON_OBJ_TYPE::OBJECT && &(*media_pack)["ferryframes"] != NULL) {
                        vector<FFJSON*>* frames = (*media_pack)["ferryframes"].val.array;
                        vector<FFJSON*>* sizes = (*media_pack)["framesizes"].val.array;
                        char* ferrymp3 = (*media_pack)["ferrymp3"].val.string;
                        int i = frames->size();
                        char* frame;
                        string fn_b(ferrymedia_store_dir + string(itoa((*media_pack)["index"].val.number)));
                        string fn;
                        int fd;
                        while (i > 0) {
                            i--;
                            frame = (*frames)[i]->val.string;
                            if ((*frames)[i]->length != (*sizes)[i]->val.number) {
                                cout << "\n" << getTime() << " FerryStream: " << fs->path << ": packetNo: " << fn_b << " frameNo: " << i << " frame size changed. Sent frame length :" << (*sizes)[i]->val.number << "; Received frame length:" << (*frames)[i]->length << ".\n";
                            }
                            fn = fn_b + "-" + string(itoa(i)) + ".jpeg";
                            fd = open(fn.c_str(), O_WRONLY | O_TRUNC | O_CREAT);
                            int s;
                            s = write(fd, frame, (*frames)[i]->length);
                            close(fd);
                            if (s >= 0) {
                                if ((debug & 4) == 4) {
                                    cout << "\n" << getTime() << " FerryStream: " << fs->path << ": frame " << fn << " successfully written.\n";
                                    fflush(stdout);
                                }
                            } else {
                                if ((debug & 1) == 1) {
                                    cout << "\n" << getTime() << " FerryStream: " << fs->path << ": frame " << fn << " save failed.\n";
                                    fflush(stdout);
                                }
                            }
                        }
                        ofstream mp3segment(fn_b + ".mp3", std::ios_base::out | std::ios_base::binary);
                        mp3segment.write(ferrymp3, (*media_pack)["ferrymp3"].length);
                        mp3segment.close();
                        //                        if ((debug & 8) == 8) {
                        //                            ofstream pktdump(fn_b + ".pktdump", std::ios_base::out | std::ios_base::binary);
                        //                            pktdump.write(truebuffer.c_str(), truebuffer.length());
                        //                            pktdump.close();
                        //                        }
                        (*media_pack)["ferryframes"].base64encode = true;
                        (*media_pack)["ferrymp3"].base64encode = true;
                        media_pack->stringify();
                        packs_to_send[fs->path] = true;
                        new_pck_chk = true;
                        if (fs->packetBuffer.size() >= fs->packetBufferSize) {
                            fs->packetBuffer.pop_front();
                        }
                        fs->packetBuffer.push_back(*media_pack);
                    }
                } catch (FFJSON::Exception e) {
                    if ((debug & 1) == 1) {
                        cout << "\n" << getTime() << " FerryStream::heart: " << fs->path << ": FFJSON::Exception:" + string(e.what());
                        fflush(stdout);
                    }
                }

            } catch (SocketException e) {
                if ((debug & 1) == 1) {
                    cout << "\n" << getTime() << " FerryStream::heart: I'm " << fs->path << " dying! Connection to " << fs->source->getDestinationIP() << "\n";
                    fflush(stdout);
                }
                fs->funeral(fs-> path);
                delete fs;
                break;
            }

        }
    }
    void(*funeral)(string path);
};

#endif	/* FERRYSTREAM_HPP */

