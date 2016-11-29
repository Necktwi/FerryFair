/* 
 * File:   FerryStream.h
 * Author: Gowtham
 *
 * Created on September 28, 2014, 5:01 PM
 */

#ifndef FERRYSTREAM_H
#define	FERRYSTREAM_H

#include "global.h"
#include <ferrybase/ServerSocket.h>
#include <ferrybase/SocketException.h>
#include <ferrybase/mystdlib.h>
#include <ferrybase/myconverters.h>
#include <ferrybase/JPEGImage.h>
#include <logger.h>
#include <FFJSON.h>
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

class FerryStream : thread {
public:

	class Exception : std::exception {
	public:

		Exception(std::string e);

		const char* what() const throw ();

		//~Exception() throw ();

	private:
		std::string identifier;
	};
	int path;

	FerryStream();

	FerryStream(const FerryStream& orig);

	FerryStream(ServerSocket::Connection * source, void(*funeral)(int));

	~FerryStream();

	void die();

	bool isConnectionAlive();

private:
	ServerSocket::Connection * source = NULL;
	thread* heartThread;
	bool suicide = false;
	string buffer = "";

	static void heart(FerryStream* fs);
	void(*funeral)(int path);
};

extern std::list<FerryStream*> deadFSList;
extern std::list<FerryStream*> liveFSList;
void cleanDeadFSList();
void cleanLiveFSList();
#endif	/* FERRYSTREAM_H */

