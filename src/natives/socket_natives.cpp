#include "extension.h"
#include "socket/SocketTypes.h"
#include "socket/SocketBase.h"
#include "socket/TcpSocket.h"
#include "socket/UdpSocket.h"
#include "socket/UnixSocket.h"
#include "core/SocketManager.h"
#include <cstring>
#include <string_view>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

static SocketBase* GetSocket(IPluginContext* context, Handle_t handle) {
	HandleError handleError;
	HandleSecurity security(context->GetIdentity(), myself->GetIdentity());

	SocketBase* socket;
	if ((handleError = handlesys->ReadHandle(handle, g_SocketHandleType, &security, (void**)&socket)) != HandleError_None) {
		context->ReportError("Invalid Socket handle %x (error %d)", handle, handleError);
		return nullptr;
	}

	return socket;
}

static cell_t SocketIsConnected(IPluginContext* context, const cell_t* params) {
	SocketBase* socket = GetSocket(context, params[1]);
	if (!socket) return 0;

	return socket->IsOpen();
}

static cell_t SocketCreate(IPluginContext* context, const cell_t* params) {
	auto type = static_cast<SocketType>(params[1]);
	if (type != SocketType::Tcp && type != SocketType::Udp && type != SocketType::Unix) {
		return context->ThrowNativeError("Invalid protocol specified");
	}

#ifdef _WIN32
	if (type == SocketType::Unix) {
		return context->ThrowNativeError("Unix sockets are not supported on Windows");
	}
#endif

	SocketBase* socket = nullptr;
	if (type == SocketType::Tcp) {
		socket = g_SocketManager.CreateSocket<TcpSocket>();
	} else if (type == SocketType::Udp) {
		socket = g_SocketManager.CreateSocket<UdpSocket>();
	} else {
		socket = g_SocketManager.CreateSocket<UnixSocket>();
	}

	if (!socket) {
		return context->ThrowNativeError("Failed to create socket");
	}

	HandleError handleError;
	Handle_t handle = handlesys->CreateHandle(g_SocketHandleType, socket, context->GetIdentity(), myself->GetIdentity(), &handleError);
	if (!handle) {
		g_SocketManager.DestroySocket(socket);
		return context->ThrowNativeError("Failed to create handle (error %d)", handleError);
	}

	socket->m_smHandle = handle;
	return handle;
}

static cell_t SocketBind(IPluginContext* context, const cell_t* params) {
	SocketBase* socket = GetSocket(context, params[1]);
	if (!socket) return 0;

	if (params[3] < 0 || params[3] > 65535) return context->ThrowNativeError("Invalid port specified");

	char* hostname = nullptr;
	context->LocalToString(params[2], &hostname);

	return socket->Bind(hostname, static_cast<uint16_t>(params[3]), false);
}

static cell_t SocketConnect(IPluginContext* context, const cell_t* params) {
	SocketBase* socket = GetSocket(context, params[1]);
	if (!socket) return 0;
	if (params[3] < 0 || params[3] > 65535) return context->ThrowNativeError("Invalid port specified");

	char* hostname = nullptr;
	context->LocalToString(params[2], &hostname);

	if (socket->IsOpen()) return context->ThrowNativeError("Socket is already connected");

	return socket->Connect(hostname, static_cast<uint16_t>(params[3]));
}

static cell_t SocketDisconnect(IPluginContext* context, const cell_t* params) {
	SocketBase* socket = GetSocket(context, params[1]);
	if (!socket) return 0;

	if (!socket->IsOpen()) return context->ThrowNativeError("Socket is not connected/listening");

	return socket->Disconnect();
}

static cell_t SocketCloseReset(IPluginContext* context, const cell_t* params) {
	SocketBase* socket = GetSocket(context, params[1]);
	if (!socket) return 0;

	if (socket->GetType() != SocketType::Tcp) return context->ThrowNativeError("CloseReset only works for TCP sockets");

	if (!socket->IsOpen()) return context->ThrowNativeError("Socket is not connected");

	return socket->CloseReset();
}

static cell_t SocketListen(IPluginContext* context, const cell_t* params) {
	SocketBase* socket = GetSocket(context, params[1]);
	if (!socket) return 0;

	if (socket->IsOpen()) return context->ThrowNativeError("Socket is already open");

	return socket->Listen();
}

static cell_t SocketSend(IPluginContext* context, const cell_t* params) {
	SocketBase* socket = GetSocket(context, params[1]);
	if (!socket) return 0;

	char* rawData = nullptr;
	context->LocalToString(params[2], &rawData);

	std::string_view data;
	if (params[3] == -1) {
		data = rawData;
	} else {
		data = std::string_view(rawData, params[3]);
	}

	if (!socket->IsOpen()) return context->ThrowNativeError("Can't send, socket is not connected");

	return socket->Send(data);
}

static cell_t SocketSendTo(IPluginContext* context, const cell_t* params) {
	SocketBase* socket = GetSocket(context, params[1]);
	if (!socket) return 0;

	if (socket->GetType() == SocketType::Tcp)
		return context->ThrowNativeError("This native doesn't support connection orientated protocols");

	char* rawData = nullptr;
	context->LocalToString(params[2], &rawData);

	std::string_view data;
	if (params[3] == -1) {
		data = rawData;
	} else {
		data = std::string_view(rawData, params[3]);
	}

	char* hostname = nullptr;
	context->LocalToString(params[4], &hostname);

	return socket->SendTo(data, hostname, static_cast<uint16_t>(params[5]));
}

static cell_t SocketSetOption(IPluginContext* context, const cell_t* params) {
	auto option = static_cast<SocketOption>(params[2]);

	switch (option) {
		case SocketOption::ConcatenateCallbacks:
		case SocketOption::ForceFrameLock:
		case SocketOption::CallbacksPerFrame:
		case SocketOption::DebugMode:
			g_GlobalOptions.Set(option, params[3]);
			return true;
		default:
			break;
	}

	SocketBase* socket = GetSocket(context, params[1]);
	if (!socket) return 0;

	return socket->SetOption(option, params[3]);
}

static cell_t SocketSetReceiveCallback(IPluginContext* context, const cell_t* params) {
	SocketBase* socket = GetSocket(context, params[1]);
	if (!socket) return 0;

	auto& callback = socket->GetCallback(CallbackEvent::Receive);
	callback.function = context->GetFunctionById(params[2]);
	callback.data = params[3];
	return true;
}

static cell_t SocketSetDisconnectCallback(IPluginContext* context, const cell_t* params) {
	SocketBase* socket = GetSocket(context, params[1]);
	if (!socket) return 0;

	auto& callback = socket->GetCallback(CallbackEvent::Disconnect);
	callback.function = context->GetFunctionById(params[2]);
	callback.data = params[3];
	return true;
}

static cell_t SocketSetErrorCallback(IPluginContext* context, const cell_t* params) {
	SocketBase* socket = GetSocket(context, params[1]);
	if (!socket) return 0;

	auto& callback = socket->GetCallback(CallbackEvent::Error);
	callback.function = context->GetFunctionById(params[2]);
	callback.data = params[3];
	return true;
}

static cell_t SocketSetConnectCallback(IPluginContext* context, const cell_t* params) {
	SocketBase* socket = GetSocket(context, params[1]);
	if (!socket) return 0;

	auto& callback = socket->GetCallback(CallbackEvent::Connect);
	callback.function = context->GetFunctionById(params[2]);
	callback.data = params[3];
	return true;
}

static cell_t SocketSetIncomingCallback(IPluginContext* context, const cell_t* params) {
	SocketBase* socket = GetSocket(context, params[1]);
	if (!socket) return 0;

	auto& callback = socket->GetCallback(CallbackEvent::Incoming);
	callback.function = context->GetFunctionById(params[2]);
	callback.data = params[3];
	return true;
}

static cell_t SocketSetListenCallback(IPluginContext* context, const cell_t* params) {
	SocketBase* socket = GetSocket(context, params[1]);
	if (!socket) return 0;

	auto& callback = socket->GetCallback(CallbackEvent::Listen);
	callback.function = context->GetFunctionById(params[2]);
	callback.data = params[3];
	return true;
}

static cell_t SocketGetHostName(IPluginContext* context, const cell_t* params) {
	char* destination = nullptr;
	context->LocalToString(params[1], &destination);

	char hostname[256];
	if (gethostname(hostname, sizeof(hostname)) == 0) {
		size_t length = strlen(hostname);
		if (length > static_cast<size_t>(params[2] - 1)) {
			length = params[2] - 1;
		}
		std::memcpy(destination, hostname, length);
		destination[length] = '\0';
		return true;
	}

	destination[0] = '\0';
	return false;
}

static cell_t SocketGetLocalAddress(IPluginContext* context, const cell_t* params) {
	SocketBase* socket = GetSocket(context, params[1]);
	if (!socket) return 0;

	std::string address;
	if (socket->GetType() == SocketType::Tcp) {
		address = static_cast<TcpSocket*>(socket)->GetLocalEndpoint().address;
	} else if (socket->GetType() == SocketType::Udp) {
		address = static_cast<UdpSocket*>(socket)->GetLocalEndpoint().address;
	} else {
		address = static_cast<UnixSocket*>(socket)->GetPath();
	}

	char* destination = nullptr;
	context->LocalToString(params[2], &destination);

	size_t length = address.length();
	if (length > static_cast<size_t>(params[3] - 1)) {
		length = params[3] - 1;
	}
	std::memcpy(destination, address.c_str(), length);
	destination[length] = '\0';

	return true;
}

static cell_t SocketGetLocalPort(IPluginContext* context, const cell_t* params) {
	SocketBase* socket = GetSocket(context, params[1]);
	if (!socket) return 0;

	// Unix sockets don't have ports
	if (socket->GetType() == SocketType::Unix) return 0;

	RemoteEndpoint endpoint;
	if (socket->GetType() == SocketType::Tcp) {
		endpoint = static_cast<TcpSocket*>(socket)->GetLocalEndpoint();
	} else {
		endpoint = static_cast<UdpSocket*>(socket)->GetLocalEndpoint();
	}

	return endpoint.port;
}

extern const sp_nativeinfo_t socket_natives[] = {
	{"Socket.Socket",                   SocketCreate},
	{"Socket.Bind",                     SocketBind},
	{"Socket.Connect",                  SocketConnect},
	{"Socket.Disconnect",               SocketDisconnect},
	{"Socket.CloseReset",               SocketCloseReset},
	{"Socket.Listen",                   SocketListen},
	{"Socket.Send",                     SocketSend},
	{"Socket.SendTo",                   SocketSendTo},
	{"Socket.SetOption",                SocketSetOption},
	{"Socket.SetReceiveCallback",       SocketSetReceiveCallback},
	{"Socket.SetDisconnectCallback",    SocketSetDisconnectCallback},
	{"Socket.SetErrorCallback",         SocketSetErrorCallback},
	{"Socket.SetConnectCallback",       SocketSetConnectCallback},
	{"Socket.SetIncomingCallback",      SocketSetIncomingCallback},
	{"Socket.SetListenCallback",        SocketSetListenCallback},
	{"Socket.GetHostName",              SocketGetHostName},
	{"Socket.GetLocalAddress",          SocketGetLocalAddress},
	{"Socket.GetLocalPort",             SocketGetLocalPort},
	{"Socket.Connected.get",            SocketIsConnected},
	{nullptr,                           nullptr},
};