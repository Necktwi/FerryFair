/*
 * Gowtham Kudupudi 06/08/2018
 * ©FerryFair
 */

#include "Authentication.h"
#include <string.h>

using namespace std;

int converse (
   int MsgNum, const struct pam_message** MsgPPtr,
   struct pam_response** RespPPtr, void* DataPtr
) {
   return ConversationCntxtPtr->_converse (MsgNum, MsgPPtr, RespPPtr, DataPtr);
}

Authentication_::Authentication_ (
   const string& Username, const string& Password
) :
Username (Username), Password (Password)
{
   ConversationCntxtPtr = this;
   const struct pam_conv Conversation = {&converse, NULL};
   _State = pam_start (
      "common-auth", Username .c_str(), &Conversation, &_AuthHandlePtr
   );
   _State = pam_authenticate (_AuthHandlePtr, 0);
}

int Authentication_::_converse (
   int MsgNum, const struct pam_message** MsgPPtr,
   struct pam_response** RespPPtr, void* DataPtr
) {
   if (MsgNum <= 0 || MsgNum > PAM_MAX_NUM_MSG)
      return (PAM_CONV_ERR);
   if ((_RespPtr = (pam_response*) calloc (MsgNum, sizeof *_RespPtr)) == NULL)
      return (PAM_BUF_ERR);
   _RespPtr->resp_retcode = 0;
   _RespPtr->resp = strdup (Password.c_str());
   *RespPPtr = _RespPtr;
   return (PAM_SUCCESS);
}

bool Authentication_::is_valid () {
   return _State == PAM_SUCCESS;
}

void Authentication_::invalidate () {
   if (_State == PAM_SUCCESS) {
      _State = pam_end(_AuthHandlePtr, _State);
   }
}

