/* Gowtham Kudupudi 06/08/2018
 * ©FerryFair
 * */

#ifndef Authuntication_h
#define Authuntication_h

#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <security/pam_appl.h>
#include <unistd.h>

class Authentication_ {
public:
   Authentication_ (const std::string& Username, const std::string& Password);
   bool is_valid ();
   void invalidate ();
   
   const std::string Username;
   const std::string Password;
   
   friend int converse (
      int MsgNum, const struct pam_message** MsgPPtr,
      struct pam_response** RespPPtr, void* DataPtr
   );

private:
   int _converse (
      int MsgNum, const struct pam_message** MsgPPtr,
      struct pam_response** RespPPtr, void* DataPtr
   );
   
   struct pam_response* _RespPtr =        NULL;
   int                  _State =          0;
   pam_handle_t*        _AuthHandlePtr =  NULL;
   
};

static thread_local Authentication_* ConversationCntxtPtr = NULL;

int converse (
   int MsgNum, const struct pam_message** MsgPPtr,
   struct pam_response** RespPPtr, void* DataPtr
);


#endif /* Authuntication_h */

