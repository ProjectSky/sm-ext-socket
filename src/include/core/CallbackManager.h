#pragma once

#include "socket/SocketTypes.h"
#include <deque>
#include <mutex>
#include <memory>
#include <string>
#include <variant>

class SocketBase;
struct SocketWrapper;

struct CallbackPayload {
	std::string receiveData;
	RemoteEndpoint endpoint;  // Used for sender/remote/local depending on event
	SocketBase* newSocket = nullptr;

	SocketError errorType = SocketError::None;
	int errorCode = 0;
};

class QueuedCallback {
public:
	QueuedCallback(CallbackEvent event, SocketBase* socket);
	QueuedCallback(CallbackEvent event, SocketBase* socket, const RemoteEndpoint& localEndpoint);
	QueuedCallback(CallbackEvent event, SocketBase* socket, const char* data, size_t length);
	QueuedCallback(CallbackEvent event, SocketBase* socket, const char* data, size_t length, const RemoteEndpoint& sender);
	QueuedCallback(CallbackEvent event, SocketBase* socket, SocketBase* newSocket, const RemoteEndpoint& endpoint);
	QueuedCallback(CallbackEvent event, SocketBase* socket, SocketError errorType, int errorCode);
	~QueuedCallback() = default;

	[[nodiscard]] bool IsExecutable() const;
	[[nodiscard]] bool IsValid() const;
	void Execute();
	[[nodiscard]] CallbackEvent GetEvent() const { return m_event; }
	[[nodiscard]] SocketWrapper* GetSocketWrapper() const { return m_socketWrapper; }

private:
	void ExecuteImpl();

	CallbackEvent m_event;
	SocketWrapper* m_socketWrapper = nullptr;
	CallbackPayload m_payload;
};

class CallbackManager {
public:
	void Enqueue(std::unique_ptr<QueuedCallback> callback);
	void Enqueue(CallbackEvent event, SocketBase* socket);
	void Enqueue(CallbackEvent event, SocketBase* socket, const RemoteEndpoint& localEndpoint);
	void Enqueue(CallbackEvent event, SocketBase* socket, const char* data, size_t length);
	void Enqueue(CallbackEvent event, SocketBase* socket, const char* data, size_t length, const RemoteEndpoint& sender);
	void Enqueue(CallbackEvent event, SocketBase* socket, SocketBase* newSocket);
	void Enqueue(CallbackEvent event, SocketBase* socket, SocketError errorType, int errorCode);
	void RemoveByWrapper(SocketWrapper* wrapper);
	void ProcessPendingCallbacks();

private:
	std::unique_ptr<QueuedCallback> DequeueNext();
	std::deque<std::unique_ptr<QueuedCallback>> m_queue;
	std::mutex m_queueMutex;
};

extern CallbackManager g_CallbackManager;