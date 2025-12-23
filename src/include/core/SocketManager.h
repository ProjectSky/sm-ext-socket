#pragma once

#include "socket/SocketTypes.h"
#include "socket/SocketBase.h"
#include "socket/TcpSocket.h"
#include "socket/UdpSocket.h"
#include <unordered_map>
#include <mutex>
#include <memory>

struct SocketWrapper {
	SocketWrapper(SocketBase* socketInstance, SocketType type)
		: socket(socketInstance), socketType(type) {}
	~SocketWrapper();

	SocketBase* socket;
	SocketType socketType;
};

class SocketManager {
public:
	SocketManager() = default;
	~SocketManager();

	SocketManager(const SocketManager&) = delete;
	SocketManager& operator=(const SocketManager&) = delete;

	void Shutdown();

	SocketWrapper* FindWrapper(const SocketBase* socket);

	template<typename T>
	T* CreateSocket();

	void DestroySocket(SocketWrapper* wrapper);

	void Start();
	void Stop();

private:
	std::unordered_map<const SocketBase*, SocketWrapper*> m_socketMap;
	std::mutex m_socketMapMutex;
};

template<typename T>
T* SocketManager::CreateSocket() {
	std::scoped_lock lock(m_socketMapMutex);

	T* socket = new T();
	SocketType type = socket->GetType();

	auto* wrapper = new SocketWrapper(socket, type);
	m_socketMap[socket] = wrapper;

	return socket;
}

extern SocketManager g_SocketManager;