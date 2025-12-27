#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <unordered_map>
#include <uv.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

// Forward declarations
class SocketBase;
class TcpSocket;
class UdpSocket;
#ifndef _WIN32
class UnixSocket;
#endif
class EventLoop;

enum class SocketType {
	Tcp = 1,
	Udp = 2,
	Unix = 3
};

enum class SocketError {
	None = 0,
	EmptyHost = 1,
	NoHost = 2,
	ConnectError = 3,
	SendError = 4,
	BindError = 5,
	RecvError = 6,
	ListenError = 7
};

enum class SocketOption {
	// SourceMod level options
	ConcatenateCallbacks = 1,
	ForceFrameLock = 2,
	CallbacksPerFrame = 3,
	// Socket level options
	Broadcast = 4,
	ReuseAddr = 5,
	KeepAlive = 6,
	Linger = 7,
	OOBInline = 8,
	SendBuffer = 9,
	ReceiveBuffer = 10,
	DontRoute = 11,
	ReceiveLowWatermark = 12,
	ReceiveTimeout = 13,
	SendLowWatermark = 14,
	SendTimeout = 15,
	// Extension options
	DebugMode = 16,
	ConnectTimeout = 17,
	AutoFreeHandle = 18
};

enum class CallbackEvent {
	Connect = 0,
	Disconnect = 1,
	Incoming = 2,
	Receive = 3,
	Error = 4,
	Listen = 5,
	Count = 6
};

struct RemoteEndpoint {
	std::string address;
	uint16_t port = 0;
};

RemoteEndpoint ExtractEndpoint(const sockaddr* addr);

struct PendingOption {
	SocketOption option;
	int value;

	PendingOption(SocketOption opt, int val) : option(opt), value(val) {}
};

// Global options (extension-level settings)
class GlobalOptions {
public:
	static GlobalOptions& Instance() {
		static GlobalOptions instance;
		return instance;
	}

	void Set(SocketOption option, int value) { m_options[option] = value; }
	int Get(SocketOption option) const {
		auto it = m_options.find(option);
		return it != m_options.end() ? it->second : GetDefault(option);
	}

private:
	static int GetDefault(SocketOption option) {
		switch (option) {
			case SocketOption::CallbacksPerFrame: return 1;
			default: return 0;
		}
	}

	std::unordered_map<SocketOption, int> m_options;
};

inline GlobalOptions& g_GlobalOptions = GlobalOptions::Instance();