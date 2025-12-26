#include "core/SocketManager.h"
#include "core/EventLoop.h"
#include <cassert>

SocketManager g_SocketManager;

SocketManager::~SocketManager() {
	if (!m_socketMap.empty()) {
		Shutdown();
	}
}

void SocketManager::Shutdown() {
	Stop();

	// Delete all sockets
	m_socketMap.for_each([](const SocketBase* key, bool) {
		delete const_cast<SocketBase*>(key);
	});

	m_socketMap.clear();
}

bool SocketManager::IsValidSocket(const SocketBase* socket) {
	if (!socket) return false;
	if (socket->IsDeleted()) return false;
	return m_socketMap.find(socket);
}

void SocketManager::DestroySocket(SocketBase* socket) {
	assert(socket);

	// Mark as deleted first
	socket->MarkDeleted();

	// Remove from map
	m_socketMap.remove(socket);

	// Delete socket
	delete socket;
}

void SocketManager::Start() {
	g_EventLoop.Start();
}

void SocketManager::Stop() {
	g_EventLoop.Stop();
}