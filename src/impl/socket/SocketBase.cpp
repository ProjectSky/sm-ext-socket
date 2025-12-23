#include "socket/SocketBase.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

SocketBase::SocketBase(SocketType type) : m_type(type) {}

SocketBase::~SocketBase() {
	std::unique_lock<std::shared_mutex> lock(m_handlerMutex);
	std::scoped_lock optionsLock(m_optionsMutex);
	while (!m_pendingOptions.empty()) {
		m_pendingOptions.pop();
	}
}

void SocketBase::QueueOption(SocketOption option, int value) {
	std::scoped_lock lock(m_optionsMutex);
	m_pendingOptions.emplace(option, value);
}

bool SocketBase::SetSocketOption(uv_os_sock_t socketFd, SocketOption option, int value) {
	if (socketFd == (uv_os_sock_t)-1) {
		return false;
	}

	auto setBool = [socketFd, value](int optName) {
		int boolValue = !!value;
		return setsockopt(socketFd, SOL_SOCKET, optName, reinterpret_cast<const char*>(&boolValue), sizeof(boolValue)) == 0;
	};

	auto setInt = [socketFd, &value](int optName) {
		return setsockopt(socketFd, SOL_SOCKET, optName, reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
	};

	auto setTimeout = [socketFd, value](int optName) {
#ifdef _WIN32
		DWORD timeoutMs = value;
		return setsockopt(socketFd, SOL_SOCKET, optName, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs)) == 0;
#else
		struct timeval tv = { value / 1000, (value % 1000) * 1000 };
		return setsockopt(socketFd, SOL_SOCKET, optName, reinterpret_cast<const char*>(&tv), sizeof(tv)) == 0;
#endif
	};

	switch (option) {
		case SocketOption::Broadcast:   return setBool(SO_BROADCAST);
		case SocketOption::ReuseAddr:   return setBool(SO_REUSEADDR);
		case SocketOption::KeepAlive:   return setBool(SO_KEEPALIVE);
		case SocketOption::DontRoute:   return setBool(SO_DONTROUTE);
		case SocketOption::OOBInline:   return setBool(SO_OOBINLINE);
		case SocketOption::SendBuffer:      return setInt(SO_SNDBUF);
		case SocketOption::ReceiveBuffer:   return setInt(SO_RCVBUF);
#ifdef SO_RCVLOWAT
		case SocketOption::ReceiveLowWatermark: return setInt(SO_RCVLOWAT);
#endif
#ifdef SO_SNDLOWAT
		case SocketOption::SendLowWatermark:    return setInt(SO_SNDLOWAT);
#endif
		case SocketOption::ReceiveTimeout:  return setTimeout(SO_RCVTIMEO);
		case SocketOption::SendTimeout:     return setTimeout(SO_SNDTIMEO);
		case SocketOption::Linger: {
			struct linger opt = { (value > 0) ? 1 : 0, value };
			return setsockopt(socketFd, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
		}
		default:
			return false;
	}
}