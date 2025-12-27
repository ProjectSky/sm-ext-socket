#include "core/SocketManager.h"
#include "core/EventLoop.h"

SocketManager g_SocketManager;

SocketManager::~SocketManager() {
	if (!m_sockets.empty()) {
		Shutdown();
	}
}

void SocketManager::Shutdown() {
	Stop();

	// Delete all sockets
	for (auto* socket : m_sockets) {
		delete socket;
	}
	m_sockets.clear();
}

void SocketManager::DestroySocket(SocketBase* socket) {
	if (!socket) return;

	// Mark as deleted first
	socket->MarkDeleted();

	// Remove from set
	m_sockets.erase(socket);

	// Delete socket
	delete socket;
}

void SocketManager::Start() {
	g_EventLoop.Start();
}

void SocketManager::Stop() {
	g_EventLoop.Stop();
}