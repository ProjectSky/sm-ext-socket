#include "core/SocketManager.h"
#include "core/EventLoop.h"
#include "core/CallbackManager.h"
#include <cassert>

SocketManager g_SocketManager;

SocketWrapper::~SocketWrapper() {
	delete socket;
	g_CallbackManager.RemoveByWrapper(this);
}

SocketManager::~SocketManager() {
	if (!m_socketMap.empty()) {
		Shutdown();
	}
}

void SocketManager::Shutdown() {
	Stop();

	std::scoped_lock lock(m_socketMapMutex);

	for (auto& [socket, wrapper] : m_socketMap) {
		delete wrapper;
	}

	m_socketMap.clear();
}

SocketWrapper* SocketManager::FindWrapper(const SocketBase* socket) {
	std::scoped_lock lock(m_socketMapMutex);

	auto it = m_socketMap.find(socket);
	return (it != m_socketMap.end()) ? it->second : nullptr;
}

void SocketManager::DestroySocket(SocketWrapper* wrapper) {
	assert(wrapper);

	{
		std::scoped_lock lock(m_socketMapMutex);
		m_socketMap.erase(wrapper->socket);
	}

	delete wrapper;
}

void SocketManager::Start() {
	g_EventLoop.Start();
}

void SocketManager::Stop() {
	g_EventLoop.Stop();
}