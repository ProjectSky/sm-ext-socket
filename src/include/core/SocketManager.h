#pragma once

#include "socket/SocketTypes.h"
#include "socket/SocketBase.h"
#include "socket/TcpSocket.h"
#include "socket/UdpSocket.h"
#ifndef _WIN32
#include "socket/UnixSocket.h"
#endif
#include <unordered_set>

/**
 * Socket manager for tracking active sockets.
 *
 * Thread model:
 * - All operations are called from game thread only
 * - UV thread uses socket->IsDeleted() atomic flag for validation
 */
class SocketManager {
public:
	SocketManager() = default;
	~SocketManager();

	SocketManager(const SocketManager&) = delete;
	SocketManager& operator=(const SocketManager&) = delete;

	void Shutdown();

	/**
	 * Create a new socket of the specified type.
	 * Must be called from game thread.
	 */
	template<typename T>
	T* CreateSocket();

	/**
	 * Destroy a socket.
	 * Must be called from game thread.
	 */
	void DestroySocket(SocketBase* socket);

	void Start();
	void Stop();

private:
	std::unordered_set<SocketBase*> m_sockets;
};

template<typename T>
T* SocketManager::CreateSocket() {
	T* socket = new T();
	m_sockets.insert(socket);
	return socket;
}

extern SocketManager g_SocketManager;