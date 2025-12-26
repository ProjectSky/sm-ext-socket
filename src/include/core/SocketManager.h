#pragma once

#include "socket/SocketTypes.h"
#include "socket/SocketBase.h"
#include "socket/TcpSocket.h"
#include "socket/UdpSocket.h"
#include "socket/UnixSocket.h"
#include "lockfree/LockFreeMap.h"

/**
 * Lock-free socket manager using LockFreeMap.
 *
 * Thread model:
 * - Game thread: creates and destroys sockets
 * - UV thread: looks up sockets for callback enqueuing
 *
 * The LockFreeMap allows concurrent reads from UV thread while
 * game thread performs modifications.
 */
class SocketManager {
public:
	SocketManager() = default;
	~SocketManager();

	SocketManager(const SocketManager&) = delete;
	SocketManager& operator=(const SocketManager&) = delete;

	void Shutdown();

	/**
	 * Check if a socket exists and is not deleted.
	 * Thread-safe, can be called from any thread.
	 */
	bool IsValidSocket(const SocketBase* socket);

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
	// Lock-free set for socket registry (value is just a marker, we use the key)
	LockFreeMap<const SocketBase*, bool> m_socketMap;
};

template<typename T>
T* SocketManager::CreateSocket() {
	T* socket = new T();
	m_socketMap.insert(socket, true);
	return socket;
}

extern SocketManager g_SocketManager;