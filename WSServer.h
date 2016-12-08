/* 
 * File:   WSServer.h
 * Author: Gowtham
 *
 * Created on December 12, 2013, 6:34 PM
 */

#ifndef WSSERVER_H
#define WSSERVER_H

#define MAX_ECHO_PAYLOAD 1024
#define LWS_NO_CLIENT

#include "FerryStream.h"
#include <libwebsockets.h>
#include <FFJSON.h>
#include <string>
#include <map>
#include <list>
#include <thread>

using namespace std;
using namespace placeholders;
class WSServer {
public:

	enum FraggleStates {
		FRAGSTATE_INIT_PCK,
		FRAGSTATE_NEW_PCK,
		FRAGSTATE_MORE_FRAGS,
		FRAGSTATE_INCOMPREHENSIBLE_INIT_MSG,
		FRAGSTATE_SEND_ERRMSG
	};

	enum Protocol {
		PROTOCOL_HTTP = 0,
		PROTOCOL_FFJSON
	};
    WSServer(
            const char* pcHostName,
            int iDebugLevel=15,
            int iPort=8080,
            int iSecurePort=0,
            const char* pcSSLCertFilePath="",
            const char* pcSSLPrivKeyFilePath="",
            const char* pcSSLCAFilePath="",
            bool bDaemonize=false,
            int iRateUs=0,
            const char* pcInterface="",
            const char* pcClient="",
            int iOpts=0,
#ifndef LWS_NO_CLIENT
            const char* pcAddress="",
            unsigned int uiOldus = 0,
            struct lws* pWSI=NULL,
#endif
            int iSysLogOptions=0
    );
    virtual ~WSServer();
    char m_pcHostName[256];
    int m_iRateUs;
    int m_iDebugLevel;
    int m_iPort;
    int m_iSecurePort;
    bool m_bDaemonize;
    char m_pcSSLCertFilePath[256];
    char m_pcSSLPrivKeyFilePath[256];
    char m_pcSSLCAFilePath[256];
    char m_pcClient[256];
    char m_pcInterface[128];
    int m_iOpts;
    int m_iSysLogOptions;
    struct lws_context_creation_info m_Info;
    struct lws_context_creation_info m_SecureInfo;
    struct lws_context* m_pContext;
    struct lws_context* m_pSecureContext;
#ifndef LWS_NO_CLIENT
    char m_pcAddress[256];
	unsigned int oldus = 0;
    struct lws* m_pWSI;
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

	struct user_session {
		unsigned int session_id = 0;
		unsigned int user_id = 0;
		char username[32] = "";
		bool is_authenticated = false;
		time_t last_access_time = 0;
	};

	struct per_session_data__http {
		int fd;
		char vhost[128];
		string* payload = NULL;
		const char* offset = NULL;
		unsigned int session_id = 0;
	};

	struct per_session_data__fairplay {
		unsigned char buf[LWS_SEND_BUFFER_PRE_PADDING + MAX_ECHO_PAYLOAD + LWS_SEND_BUFFER_POST_PADDING];
		unsigned int len;
		unsigned int index;
		bool initiated;
		std::list<FFJSON*>::iterator i;
		std::list<FFJSON*>* packs;

		/**
		 * It is set if the packet in the list pointed by i is the last packet in the list 
		 * when the last packet was sent. when sending a packet if it is set, 'i' is incremented
		 * before sending the packet.
		 */
		bool endHit;
		bool morechunks;

		/**
		 * when there are more chunks to send the initByte is assigned with address pointing
		 * to next chunk.
		 */
		unsigned char* initByte;
		unsigned long sum;
		bool deletePayload = false;

		/**
		 * 
		 */
		enum WSServer::FraggleStates state;

		/**
		 */
		std::string* payload;
	};

	lws_protocols protocols[3] = {
		/* first protocol must always be HTTP handler */
		{
			"http-only", /* name */
            WSServer::callback_http, /* callback */
			sizeof (struct per_session_data__http), /* per_session_data_size */
			0, /* max frame size / rx buffer */
		},
		{
			"fairplay",
			WSServer::callbackFairPlayWS,
			sizeof (struct per_session_data__fairplay),
			10,
		},
		{ NULL, NULL, 0, 0} /* terminator */
	};

private:
    int heart();
	int static callbackFairPlayWS(struct lws *wsi,
			enum lws_callback_reasons reason,
			void *user, void *in, size_t len);
    static int callback_http(struct lws *wsi,
			enum lws_callback_reasons reason,
			void *user, void *in, size_t len);
	static std::map<unsigned int, user_session*> user_sessions;
	static unsigned int session_count;
	static unsigned int create_session();
	static unsigned int valid_session(unsigned int session_id);
	std::thread* heartThread;
	static FFJSON models;
};
#endif /* WSSERVER_H */
