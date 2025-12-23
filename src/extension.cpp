#include "extension.h"
#include "core/SocketManager.h"
#include "core/CallbackManager.h"

SocketExtension g_SocketExt;
SMEXT_LINK(&g_SocketExt);

HandleType_t g_SocketHandleType;

static void OnGameFrame(bool simulating) {
	g_CallbackManager.ProcessPendingCallbacks();
}

bool SocketExtension::SDK_OnLoad(char* error, size_t maxlen, bool late) {
	HandleError handleError;
	HandleAccess accessDefaults;
	handlesys->InitAccessDefaults(nullptr, &accessDefaults);
	accessDefaults.access[HandleAccess_Delete] = 0;
	g_SocketHandleType = handlesys->CreateType("Socket", this, 0, nullptr, &accessDefaults, myself->GetIdentity(), &handleError);
	if (!g_SocketHandleType) {
		snprintf(error, maxlen, "Failed to create handle type (error %d)", handleError);
		return false;
	}

	sharesys->AddNatives(myself, socket_natives);
	sharesys->RegisterLibrary(myself, "socket");

	smutils->AddGameFrameHook(&OnGameFrame);
	g_SocketManager.Start();

	return true;
}

void SocketExtension::SDK_OnUnload() {
	smutils->RemoveGameFrameHook(&OnGameFrame);
	handlesys->RemoveType(g_SocketHandleType, myself->GetIdentity());
	g_SocketManager.Shutdown();
}

void SocketExtension::OnHandleDestroy(HandleType_t type, void* object) {
	if (object != nullptr) {
		g_SocketManager.DestroySocket(static_cast<SocketWrapper*>(object));
	}
}