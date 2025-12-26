#include "core/CallbackManager.h"
#include "socket/SocketBase.h"
#include "extension.h"
#include <cstring>
#include <cstdlib>

CallbackManager g_CallbackManager;

void CallbackManager::EnqueueConnect(SocketBase* socket, const RemoteEndpoint& endpoint) {
	QueuedConnectEvent event;
	event.socket = socket;
	event.remoteEndpoint = endpoint;

	if (!m_connectQueue.try_enqueue(std::move(event))) {
		if (g_GlobalOptions.Get(SocketOption::DebugMode)) {
			smutils->LogError(myself, "[Socket] Connect queue full, dropping event");
		}
	}
}

void CallbackManager::EnqueueDisconnect(SocketBase* socket) {
	QueuedDisconnectEvent event;
	event.socket = socket;

	if (!m_disconnectQueue.try_enqueue(std::move(event))) {
		if (g_GlobalOptions.Get(SocketOption::DebugMode)) {
			smutils->LogError(myself, "[Socket] Disconnect queue full, dropping event");
		}
	}
}

void CallbackManager::EnqueueListen(SocketBase* socket, const RemoteEndpoint& localEndpoint) {
	QueuedListenEvent event;
	event.socket = socket;
	event.localEndpoint = localEndpoint;

	if (!m_listenQueue.try_enqueue(std::move(event))) {
		if (g_GlobalOptions.Get(SocketOption::DebugMode)) {
			smutils->LogError(myself, "[Socket] Listen queue full, dropping event");
		}
	}
}

void CallbackManager::EnqueueIncoming(SocketBase* socket, SocketBase* newSocket, const RemoteEndpoint& remoteEndpoint) {
	QueuedIncomingEvent event;
	event.socket = socket;
	event.newSocket = newSocket;
	event.remoteEndpoint = remoteEndpoint;

	if (!m_incomingQueue.try_enqueue(std::move(event))) {
		if (g_GlobalOptions.Get(SocketOption::DebugMode)) {
			smutils->LogError(myself, "[Socket] Incoming queue full, dropping event");
		}
	}
}

void CallbackManager::EnqueueReceive(SocketBase* socket, const char* data, size_t length) {
	RemoteEndpoint emptyEndpoint;
	EnqueueReceive(socket, data, length, emptyEndpoint);
}

void CallbackManager::EnqueueReceive(SocketBase* socket, const char* data, size_t length, const RemoteEndpoint& sender) {
	// Allocate buffer for data (consumer will free)
	char* dataCopy = static_cast<char*>(malloc(length + 1));
	if (!dataCopy) {
		if (g_GlobalOptions.Get(SocketOption::DebugMode)) {
			smutils->LogError(myself, "[Socket] Failed to allocate memory for receive data");
		}
		return;
	}
	memcpy(dataCopy, data, length);
	dataCopy[length] = '\0';

	QueuedDataEvent event;
	event.socket = socket;
	event.data = dataCopy;
	event.length = length;
	event.sender = sender;

	if (!m_dataQueue.try_enqueue(std::move(event))) {
		free(dataCopy);
		if (g_GlobalOptions.Get(SocketOption::DebugMode)) {
			smutils->LogError(myself, "[Socket] Data queue full, dropping event");
		}
	}
}

void CallbackManager::EnqueueError(SocketBase* socket, SocketError errorType, const char* errorMsg) {
	QueuedErrorEvent event;
	event.socket = socket;
	event.errorType = errorType;
	event.errorMsg = errorMsg;

	if (!m_errorQueue.try_enqueue(std::move(event))) {
		if (g_GlobalOptions.Get(SocketOption::DebugMode)) {
			smutils->LogError(myself, "[Socket] Error queue full, dropping event");
		}
	}
}

bool CallbackManager::IsSocketValid(SocketBase* socket) const {
	if (!socket) return false;
	if (socket->IsDeleted()) return false;
	return true;
}

bool CallbackManager::HasPendingCallbacks() const {
	return !m_connectQueue.empty() ||
	       !m_disconnectQueue.empty() ||
	       !m_listenQueue.empty() ||
	       !m_incomingQueue.empty() ||
	       !m_dataQueue.empty() ||
	       !m_errorQueue.empty();
}

void CallbackManager::ProcessPendingCallbacks() {
	int maxCallbacks = g_GlobalOptions.Get(SocketOption::CallbacksPerFrame);
	int processed = 0;

	// Process callbacks in round-robin fashion across all queues
	// Order: Connect -> Listen -> Incoming -> Data -> Disconnect -> Error
	while (processed < maxCallbacks) {
		bool anyProcessed = false;

		// Connect events
		QueuedConnectEvent connectEvent;
		if (m_connectQueue.try_dequeue(connectEvent)) {
			ExecuteConnect(connectEvent);
			++processed;
			anyProcessed = true;
			if (processed >= maxCallbacks) break;
		}

		// Listen events
		QueuedListenEvent listenEvent;
		if (m_listenQueue.try_dequeue(listenEvent)) {
			ExecuteListen(listenEvent);
			++processed;
			anyProcessed = true;
			if (processed >= maxCallbacks) break;
		}

		// Incoming connection events
		QueuedIncomingEvent incomingEvent;
		if (m_incomingQueue.try_dequeue(incomingEvent)) {
			ExecuteIncoming(incomingEvent);
			++processed;
			anyProcessed = true;
			if (processed >= maxCallbacks) break;
		}

		// Data receive events
		QueuedDataEvent dataEvent;
		if (m_dataQueue.try_dequeue(dataEvent)) {
			ExecuteReceive(dataEvent);
			++processed;
			anyProcessed = true;
			if (processed >= maxCallbacks) break;
		}

		// Disconnect events
		QueuedDisconnectEvent disconnectEvent;
		if (m_disconnectQueue.try_dequeue(disconnectEvent)) {
			ExecuteDisconnect(disconnectEvent);
			++processed;
			anyProcessed = true;
			if (processed >= maxCallbacks) break;
		}

		// Error events
		QueuedErrorEvent errorEvent;
		if (m_errorQueue.try_dequeue(errorEvent)) {
			ExecuteError(errorEvent);
			++processed;
			anyProcessed = true;
			if (processed >= maxCallbacks) break;
		}

		// No more events in any queue
		if (!anyProcessed) break;
	}
}

void CallbackManager::ExecuteConnect(const QueuedConnectEvent& event) {
	if (!IsSocketValid(event.socket)) return;

	auto& callbackInfo = event.socket->GetCallback(CallbackEvent::Connect);
	if (!callbackInfo.function) return;

	callbackInfo.function->PushCell(event.socket->m_smHandle);
	callbackInfo.function->PushCell(callbackInfo.data);
	callbackInfo.function->Execute(nullptr);
}

void CallbackManager::ExecuteDisconnect(const QueuedDisconnectEvent& event) {
	if (!IsSocketValid(event.socket)) return;

	auto& callbackInfo = event.socket->GetCallback(CallbackEvent::Disconnect);
	if (!callbackInfo.function) return;

	callbackInfo.function->PushCell(event.socket->m_smHandle);
	callbackInfo.function->PushCell(callbackInfo.data);
	callbackInfo.function->Execute(nullptr);

	if (event.socket->GetOption(SocketOption::AutoFreeHandle) && event.socket->m_smHandle) {
		HandleSecurity security(callbackInfo.function->GetParentContext()->GetIdentity(), myself->GetIdentity());
		handlesys->FreeHandle(event.socket->m_smHandle, &security);
	}
}

void CallbackManager::ExecuteListen(const QueuedListenEvent& event) {
	if (!IsSocketValid(event.socket)) return;

	auto& callbackInfo = event.socket->GetCallback(CallbackEvent::Listen);
	if (!callbackInfo.function) return;

	callbackInfo.function->PushCell(event.socket->m_smHandle);
	callbackInfo.function->PushString(event.localEndpoint.address.c_str());
	callbackInfo.function->PushCell(event.localEndpoint.port);
	callbackInfo.function->PushCell(callbackInfo.data);
	callbackInfo.function->Execute(nullptr);
}

void CallbackManager::ExecuteIncoming(const QueuedIncomingEvent& event) {
	if (!IsSocketValid(event.socket)) return;
	if (!event.newSocket) return;

	auto& callbackInfo = event.socket->GetCallback(CallbackEvent::Incoming);
	if (!callbackInfo.function) return;

	event.newSocket->m_smHandle = handlesys->CreateHandle(
		g_SocketHandleType,
		event.newSocket,
		callbackInfo.function->GetParentContext()->GetIdentity(),
		myself->GetIdentity(),
		nullptr);

	callbackInfo.function->PushCell(event.socket->m_smHandle);
	callbackInfo.function->PushCell(event.newSocket->m_smHandle);
	callbackInfo.function->PushString(event.remoteEndpoint.address.c_str());
	callbackInfo.function->PushCell(event.remoteEndpoint.port);
	callbackInfo.function->PushCell(callbackInfo.data);
	callbackInfo.function->Execute(nullptr);
}

void CallbackManager::ExecuteReceive(const QueuedDataEvent& event) {
	if (IsSocketValid(event.socket)) {
		auto& callbackInfo = event.socket->GetCallback(CallbackEvent::Receive);
		if (callbackInfo.function) {
			callbackInfo.function->PushCell(event.socket->m_smHandle);
			callbackInfo.function->PushStringEx(event.data, event.length + 1,
				SM_PARAM_STRING_COPY | SM_PARAM_STRING_BINARY, 0);
			callbackInfo.function->PushCell(static_cast<cell_t>(event.length));
			callbackInfo.function->PushString(event.sender.address.c_str());
			callbackInfo.function->PushCell(event.sender.port);
			callbackInfo.function->PushCell(callbackInfo.data);
			callbackInfo.function->Execute(nullptr);
		}
	}
	free(event.data);
}

void CallbackManager::ExecuteError(const QueuedErrorEvent& event) {
	if (!IsSocketValid(event.socket)) return;

	auto& callbackInfo = event.socket->GetCallback(CallbackEvent::Error);
	if (!callbackInfo.function) return;

	callbackInfo.function->PushCell(event.socket->m_smHandle);
	callbackInfo.function->PushCell(static_cast<cell_t>(event.errorType));
	callbackInfo.function->PushString(event.errorMsg);
	callbackInfo.function->PushCell(callbackInfo.data);
	callbackInfo.function->Execute(nullptr);

	if (event.socket->GetOption(SocketOption::AutoFreeHandle) && event.socket->m_smHandle) {
		HandleSecurity security(callbackInfo.function->GetParentContext()->GetIdentity(), myself->GetIdentity());
		handlesys->FreeHandle(event.socket->m_smHandle, &security);
	}
}