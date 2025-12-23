#ifndef _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_
#define _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_

#include "smsdk_ext.h"

struct SocketWrapper;

class SocketExtension : public SDKExtension, public IHandleTypeDispatch {
public:
	bool SDK_OnLoad(char* error, size_t maxlen, bool late) override;
	void SDK_OnUnload() override;
	void OnHandleDestroy(HandleType_t type, void* object) override;
};

extern HandleType_t g_SocketHandleType;
extern const sp_nativeinfo_t socket_natives[];
extern SocketExtension g_SocketExt;

#endif // _INCLUDE_SOURCEMOD_EXTENSION_PROPER_H_
