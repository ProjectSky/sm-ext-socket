#include "core/CallbackManager.h"
#include "core/SocketManager.h"
#include "socket/SocketBase.h"
#include "socket/TcpSocket.h"
#include "socket/UdpSocket.h"
#include "extension.h"

CallbackManager g_CallbackManager;

QueuedCallback::QueuedCallback(CallbackEvent event, SocketBase* socket)
	: m_event(event) {
	m_socketWrapper = g_SocketManager.FindWrapper(socket);
}

QueuedCallback::QueuedCallback(CallbackEvent event, SocketBase* socket, const RemoteEndpoint& localEndpoint)
	: m_event(event) {
	m_socketWrapper = g_SocketManager.FindWrapper(socket);
	m_payload.endpoint = localEndpoint;
}

QueuedCallback::QueuedCallback(CallbackEvent event, SocketBase* socket, const char* data, size_t length)
	: m_event(event) {
	m_socketWrapper = g_SocketManager.FindWrapper(socket);
	m_payload.receiveData.assign(data, length);
}

QueuedCallback::QueuedCallback(CallbackEvent event, SocketBase* socket, const char* data, size_t length, const RemoteEndpoint& sender)
	: m_event(event) {
	m_socketWrapper = g_SocketManager.FindWrapper(socket);
	m_payload.receiveData.assign(data, length);
	m_payload.endpoint = sender;
}

QueuedCallback::QueuedCallback(CallbackEvent event, SocketBase* socket, SocketBase* newSocket, const RemoteEndpoint& endpoint)
	: m_event(event) {
	m_socketWrapper = g_SocketManager.FindWrapper(socket);
	m_payload.newSocket = newSocket;
	m_payload.endpoint = endpoint;
}

QueuedCallback::QueuedCallback(CallbackEvent event, SocketBase* socket, SocketError errorType, int errorCode)
	: m_event(event) {
	m_socketWrapper = g_SocketManager.FindWrapper(socket);
	m_payload.errorType = errorType;
	m_payload.errorCode = errorCode;
}

bool QueuedCallback::IsValid() const {
	if (!m_socketWrapper) return false;
	if (m_event == CallbackEvent::Incoming && !m_payload.newSocket) return false;
	return true;
}

bool QueuedCallback::IsExecutable() const {
	if (!m_socketWrapper) return false;

	auto* socket = m_socketWrapper->socket;
	if (!socket) return false;

	return socket->GetCallback(m_event).function != nullptr;
}

void QueuedCallback::Execute() {
	if (!IsValid()) return;
	ExecuteImpl();
}

void QueuedCallback::ExecuteImpl() {
	auto* socket = m_socketWrapper->socket;
	if (!socket) return;

	auto& callbackInfo = socket->GetCallback(m_event);
	if (!callbackInfo.function) return;

	switch (m_event) {
		case CallbackEvent::Connect:
			callbackInfo.function->PushCell(socket->m_smHandle);
			callbackInfo.function->PushCell(callbackInfo.data);
			callbackInfo.function->Execute(nullptr);
			break;

		case CallbackEvent::Disconnect:
			callbackInfo.function->PushCell(socket->m_smHandle);
			callbackInfo.function->PushCell(callbackInfo.data);
			callbackInfo.function->Execute(nullptr);
			if (socket->GetOption(SocketOption::AutoClose) && socket->m_smHandle) {
				HandleSecurity security(callbackInfo.function->GetParentContext()->GetIdentity(), myself->GetIdentity());
				handlesys->FreeHandle(socket->m_smHandle, &security);
			}
			break;

		case CallbackEvent::Listen:
			callbackInfo.function->PushCell(socket->m_smHandle);
			callbackInfo.function->PushString(m_payload.endpoint.address.c_str());
			callbackInfo.function->PushCell(m_payload.endpoint.port);
			callbackInfo.function->PushCell(callbackInfo.data);
			callbackInfo.function->Execute(nullptr);
			break;

		case CallbackEvent::Incoming:
			if (m_payload.newSocket) {
				auto* newSocketWrapper = g_SocketManager.FindWrapper(m_payload.newSocket);
				if (newSocketWrapper) {
					m_payload.newSocket->m_smHandle = handlesys->CreateHandle(
						g_SocketHandleType,
						newSocketWrapper,
						callbackInfo.function->GetParentContext()->GetIdentity(),
						myself->GetIdentity(),
						nullptr);

					callbackInfo.function->PushCell(socket->m_smHandle);
					callbackInfo.function->PushCell(m_payload.newSocket->m_smHandle);
					callbackInfo.function->PushString(m_payload.endpoint.address.c_str());
					callbackInfo.function->PushCell(m_payload.endpoint.port);
					callbackInfo.function->PushCell(callbackInfo.data);
					callbackInfo.function->Execute(nullptr);
				}
			}
			break;

		case CallbackEvent::Receive: {
			size_t dataLength = m_payload.receiveData.length();
			callbackInfo.function->PushCell(socket->m_smHandle);
			callbackInfo.function->PushStringEx(m_payload.receiveData.data(), dataLength + 1,
				SM_PARAM_STRING_COPY | SM_PARAM_STRING_BINARY, 0);
			callbackInfo.function->PushCell(static_cast<cell_t>(dataLength));
			callbackInfo.function->PushString(m_payload.endpoint.address.c_str());
			callbackInfo.function->PushCell(m_payload.endpoint.port);
			callbackInfo.function->PushCell(callbackInfo.data);
			callbackInfo.function->Execute(nullptr);
			break;
		}

		case CallbackEvent::Error:
			callbackInfo.function->PushCell(socket->m_smHandle);
			callbackInfo.function->PushCell(static_cast<cell_t>(m_payload.errorType));
			callbackInfo.function->PushCell(m_payload.errorCode);
			callbackInfo.function->PushCell(callbackInfo.data);
			callbackInfo.function->Execute(nullptr);
			if (socket->GetOption(SocketOption::AutoClose) && socket->m_smHandle) {
				HandleSecurity security(callbackInfo.function->GetParentContext()->GetIdentity(), myself->GetIdentity());
				handlesys->FreeHandle(socket->m_smHandle, &security);
			}
			break;

		default:
			break;
	}
}

void CallbackManager::Enqueue(std::unique_ptr<QueuedCallback> callback) {
	std::scoped_lock lock(m_queueMutex);

	if (!callback->IsValid()) {
		if (g_GlobalOptions.Get(SocketOption::DebugMode)) {
			smutils->LogError(myself, "[Socket] Invalid callback (event=%d)", static_cast<int>(callback->GetEvent()));
		}
		return;
	}

	m_queue.push_back(std::move(callback));
}

void CallbackManager::Enqueue(CallbackEvent event, SocketBase* socket) {
	Enqueue(std::make_unique<QueuedCallback>(event, socket));
}

void CallbackManager::Enqueue(CallbackEvent event, SocketBase* socket, const RemoteEndpoint& localEndpoint) {
	Enqueue(std::make_unique<QueuedCallback>(event, socket, localEndpoint));
}

void CallbackManager::Enqueue(CallbackEvent event, SocketBase* socket, const char* data, size_t length) {
	Enqueue(std::make_unique<QueuedCallback>(event, socket, data, length));
}

void CallbackManager::Enqueue(CallbackEvent event, SocketBase* socket, const char* data, size_t length, const RemoteEndpoint& sender) {
	Enqueue(std::make_unique<QueuedCallback>(event, socket, data, length, sender));
}

void CallbackManager::Enqueue(CallbackEvent event, SocketBase* socket, SocketBase* newSocket) {
	RemoteEndpoint endpoint;
	if (newSocket->GetType() == SocketType::Tcp) {
		endpoint = static_cast<TcpSocket*>(newSocket)->GetRemoteEndpoint();
	}
	Enqueue(std::make_unique<QueuedCallback>(event, socket, newSocket, endpoint));
}

void CallbackManager::Enqueue(CallbackEvent event, SocketBase* socket, SocketError errorType, int errorCode) {
	Enqueue(std::make_unique<QueuedCallback>(event, socket, errorType, errorCode));
}

void CallbackManager::RemoveByWrapper(SocketWrapper* wrapper) {
	std::scoped_lock lock(m_queueMutex);

	m_queue.erase(
		std::remove_if(m_queue.begin(), m_queue.end(),
			[wrapper](const std::unique_ptr<QueuedCallback>& callback) {
				return callback->GetSocketWrapper() == wrapper;
			}),
		m_queue.end());
}

void CallbackManager::ProcessPendingCallbacks() {
	int maxCallbacks = g_GlobalOptions.Get(SocketOption::CallbacksPerFrame);
	for (int callbackIndex = 0; callbackIndex < maxCallbacks; ++callbackIndex) {
		auto callback = DequeueNext();
		if (!callback) return;
		callback->Execute();
	}
}

std::unique_ptr<QueuedCallback> CallbackManager::DequeueNext() {
	std::scoped_lock lock(m_queueMutex);

	for (auto it = m_queue.begin(); it != m_queue.end(); ++it) {
		if ((*it)->IsExecutable()) {
			auto callback = std::move(*it);
			m_queue.erase(it);
			return callback;
		} else if (g_GlobalOptions.Get(SocketOption::DebugMode)) {
			smutils->LogError(myself, "[Socket] Callback not executable (event=%d)", static_cast<int>((*it)->GetEvent()));
		}
	}

	return nullptr;
}