#include "socket/TcpSocket.h"
#include "core/EventLoop.h"
#include "core/CallbackManager.h"
#include "core/SocketManager.h"
#include <cstring>
#include <atomic>

struct TcpConnectContext {
	uv_connect_t connectRequest;
	uv_getaddrinfo_t resolverRequest;
	TcpSocket* socket;
};

struct TcpWriteContext {
	uv_write_t writeRequest;
	std::unique_ptr<char[]> buffer;
	size_t length;
	TcpSocket* socket;
};

TcpSocket::TcpSocket() : SocketBase(SocketType::Tcp) {}

TcpSocket::~TcpSocket() {
	Disconnect();
}

void TcpSocket::InitSocket() {
	uv_tcp_t* expected = nullptr;
	uv_tcp_t* newSocket = new uv_tcp_t;
	uv_tcp_init(g_EventLoop.GetLoop(), newSocket);
	newSocket->data = this;

	if (!m_socket.compare_exchange_strong(expected, newSocket,
		std::memory_order_release, std::memory_order_acquire)) {
		// Another thread already initialized the socket
		uv_close(reinterpret_cast<uv_handle_t*>(newSocket), OnClose);
		return;
	}

	if (m_localAddrSet) {
		uv_tcp_bind(newSocket, reinterpret_cast<const sockaddr*>(&m_localAddr), 0);
	}

	ApplyPendingOptions(reinterpret_cast<uv_handle_t*>(newSocket));
}

bool TcpSocket::IsOpen() const {
	uv_tcp_t* socket = m_socket.load(std::memory_order_acquire);
	return socket != nullptr && uv_is_active(reinterpret_cast<uv_handle_t*>(socket));
}

bool TcpSocket::Bind(const char* hostname, uint16_t port, bool async) {
	if (m_localAddrSet) {
		return false;
	}

	struct addrinfo hints{};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	char portString[6];
	snprintf(portString, sizeof(portString), "%hu", port);

	if (async) {
		auto* request = new uv_getaddrinfo_t;
		request->data = this;

		int result = uv_getaddrinfo(g_EventLoop.GetLoop(), request,
			[](uv_getaddrinfo_t* request, int status, struct addrinfo* addressInfo) {
				auto* socket = static_cast<TcpSocket*>(request->data);

				if (socket->IsDeleted()) {
					if (addressInfo) uv_freeaddrinfo(addressInfo);
					delete request;
					return;
				}

				if (status == 0 && addressInfo) {
					std::memcpy(&socket->m_localAddr, addressInfo->ai_addr, addressInfo->ai_addrlen);
					socket->m_localAddrSet = true;
					uv_freeaddrinfo(addressInfo);
				} else {
					g_CallbackManager.EnqueueError(socket, SocketError::BindError, uv_strerror(status));
				}

				delete request;
			},
			hostname, portString, &hints);

		if (result != 0) {
			delete request;
			return false;
		}
	} else {
		struct addrinfo* addressInfo = nullptr;
		int result = getaddrinfo(hostname, portString, &hints, &addressInfo);
		if (result != 0 || !addressInfo) {
			return false;
		}

		std::memcpy(&m_localAddr, addressInfo->ai_addr, addressInfo->ai_addrlen);
		m_localAddrSet = true;
		freeaddrinfo(addressInfo);
	}

	return true;
}

bool TcpSocket::Connect(const char* hostname, uint16_t port, bool async) {
	char portString[6];
	snprintf(portString, sizeof(portString), "%hu", port);

	struct addrinfo hints{};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	auto* context = new TcpConnectContext;
	context->socket = this;
	context->resolverRequest.data = context;

	int result = uv_getaddrinfo(g_EventLoop.GetLoop(), &context->resolverRequest, OnResolved,
		hostname, portString, &hints);

	if (result != 0) {
		delete context;
		return false;
	}

	return true;
}

void TcpSocket::OnResolved(uv_getaddrinfo_t* request, int status, struct addrinfo* addressInfo) {
	auto* context = static_cast<TcpConnectContext*>(request->data);
	auto* socket = context->socket;

	if (socket->IsDeleted()) {
		delete context;
		if (addressInfo) uv_freeaddrinfo(addressInfo);
		return;
	}

	if (status != 0 || !addressInfo) {
		g_CallbackManager.EnqueueError(socket, SocketError::ConnectError, uv_strerror(status));
		delete context;
		if (addressInfo) uv_freeaddrinfo(addressInfo);
		return;
	}

	if (socket->m_socket.load(std::memory_order_acquire) == nullptr) {
		socket->InitSocket();
	}

	if (addressInfo->ai_family == AF_INET || addressInfo->ai_family == AF_INET6) {
		socket->m_remoteEndpoint = ExtractEndpoint(addressInfo->ai_addr);
		std::atomic_thread_fence(std::memory_order_release);
		socket->m_remoteEndpointSet.store(true, std::memory_order_release);
	}

	context->connectRequest.data = context;

	uv_tcp_t* tcpSocket = socket->m_socket.load(std::memory_order_acquire);
	if (!tcpSocket) {
		g_CallbackManager.EnqueueError(socket, SocketError::ConnectError, "Socket was closed");
		delete context;
		uv_freeaddrinfo(addressInfo);
		return;
	}

	int result = uv_tcp_connect(&context->connectRequest, tcpSocket, addressInfo->ai_addr, OnConnect);
	uv_freeaddrinfo(addressInfo);

	if (result != 0) {
		g_CallbackManager.EnqueueError(socket, SocketError::ConnectError, uv_strerror(result));
		delete context;
		return;
	}

	int connectTimeout = socket->GetOption(SocketOption::ConnectTimeout);
	if (connectTimeout > 0) {
		socket->m_connectTimer = new uv_timer_t;
		uv_timer_init(g_EventLoop.GetLoop(), socket->m_connectTimer);
		socket->m_connectTimer->data = socket;
		uv_timer_start(socket->m_connectTimer, OnConnectTimeout, static_cast<uint64_t>(connectTimeout), 0);
	}
}

void TcpSocket::OnConnect(uv_connect_t* request, int status) {
	auto* context = static_cast<TcpConnectContext*>(request->data);
	auto* socket = context->socket;

	socket->CancelConnectTimeout();

	if (socket->IsDeleted()) {
		delete context;
		return;
	}

	if (status == 0) {
		RemoteEndpoint endpoint;
		if (socket->m_remoteEndpointSet.load(std::memory_order_acquire)) {
			std::atomic_thread_fence(std::memory_order_acquire);
			endpoint = socket->m_remoteEndpoint;
		}
		g_CallbackManager.EnqueueConnect(socket, endpoint);
		socket->StartReceiving();
	} else if (status != UV_ECANCELED) {
		g_CallbackManager.EnqueueError(socket, SocketError::ConnectError, uv_strerror(status));
	}

	delete context;
}

bool TcpSocket::Disconnect() {
	uv_tcp_t* socketToClose = m_socket.exchange(nullptr, std::memory_order_acq_rel);
	uv_tcp_t* acceptorToClose = m_acceptor.exchange(nullptr, std::memory_order_acq_rel);

	if (socketToClose || acceptorToClose) {
		g_EventLoop.Post([socketToClose, acceptorToClose]() {
			if (socketToClose && !uv_is_closing(reinterpret_cast<uv_handle_t*>(socketToClose))) {
				uv_close(reinterpret_cast<uv_handle_t*>(socketToClose), OnClose);
			}
			if (acceptorToClose && !uv_is_closing(reinterpret_cast<uv_handle_t*>(acceptorToClose))) {
				uv_close(reinterpret_cast<uv_handle_t*>(acceptorToClose), OnClose);
			}
		});
	}

	return true;
}

bool TcpSocket::CloseReset() {
	uv_tcp_t* socketToClose = m_socket.exchange(nullptr, std::memory_order_acq_rel);

	if (socketToClose) {
		g_EventLoop.Post([socketToClose]() {
			if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(socketToClose))) {
				uv_tcp_close_reset(socketToClose, OnClose);
			}
		});
		return true;
	}

	return false;
}

bool TcpSocket::Listen() {
	if (!m_localAddrSet) {
		return false;
	}

	g_EventLoop.Post([this]() {
		if (IsDeleted()) return;

		uv_tcp_t* expected = nullptr;
		uv_tcp_t* newAcceptor = new uv_tcp_t;
		uv_tcp_init(g_EventLoop.GetLoop(), newAcceptor);
		newAcceptor->data = this;

		if (!m_acceptor.compare_exchange_strong(expected, newAcceptor,
			std::memory_order_release, std::memory_order_acquire)) {
			// Already has an acceptor
			uv_close(reinterpret_cast<uv_handle_t*>(newAcceptor), OnClose);
			return;
		}

		int result = uv_tcp_bind(newAcceptor, reinterpret_cast<const sockaddr*>(&m_localAddr), 0);
		if (result != 0) {
			g_CallbackManager.EnqueueError(this, SocketError::BindError, uv_strerror(result));
			m_acceptor.store(nullptr, std::memory_order_release);
			uv_close(reinterpret_cast<uv_handle_t*>(newAcceptor), OnClose);
			return;
		}

		ApplyPendingOptions(reinterpret_cast<uv_handle_t*>(newAcceptor));

		result = uv_listen(reinterpret_cast<uv_stream_t*>(newAcceptor), SOMAXCONN, OnConnection);
		if (result != 0) {
			g_CallbackManager.EnqueueError(this, SocketError::ListenError, uv_strerror(result));
			return;
		}

		g_CallbackManager.EnqueueListen(this, GetLocalEndpoint());
	});

	return true;
}

void TcpSocket::OnConnection(uv_stream_t* server, int status) {
	auto* socket = static_cast<TcpSocket*>(server->data);

	if (socket->IsDeleted()) return;

	if (status < 0) {
		if (status != UV_ECANCELED) {
			g_CallbackManager.EnqueueError(socket, SocketError::ListenError, uv_strerror(status));
		}
		return;
	}

	auto* clientHandle = new uv_tcp_t;
	uv_tcp_init(g_EventLoop.GetLoop(), clientHandle);

	if (uv_accept(server, reinterpret_cast<uv_stream_t*>(clientHandle)) == 0) {
		TcpSocket* newSocket = TcpSocket::CreateFromAccepted(clientHandle);
		if (newSocket) {
			RemoteEndpoint endpoint = newSocket->GetRemoteEndpoint();
			g_CallbackManager.EnqueueIncoming(socket, newSocket, endpoint);
			newSocket->StartReceiving();
		}
	} else {
		uv_close(reinterpret_cast<uv_handle_t*>(clientHandle), OnClose);
	}
}

TcpSocket* TcpSocket::CreateFromAccepted(uv_tcp_t* clientHandle) {
	auto* socket = g_SocketManager.CreateSocket<TcpSocket>();
	socket->m_socket.store(clientHandle, std::memory_order_release);
	clientHandle->data = socket;

	sockaddr_storage peerAddress;
	int addressLength = sizeof(peerAddress);
	if (uv_tcp_getpeername(clientHandle, reinterpret_cast<sockaddr*>(&peerAddress), &addressLength) == 0) {
		socket->m_remoteEndpoint = ExtractEndpoint(reinterpret_cast<sockaddr*>(&peerAddress));
		std::atomic_thread_fence(std::memory_order_release);
		socket->m_remoteEndpointSet.store(true, std::memory_order_release);
	}

	return socket;
}

void TcpSocket::StartReceiving() {
	uv_tcp_t* socket = m_socket.load(std::memory_order_acquire);
	if (!socket) return;

	uv_read_start(reinterpret_cast<uv_stream_t*>(socket), OnAllocBuffer, OnRead);
}

void TcpSocket::OnAllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buffer) {
	auto* socket = static_cast<TcpSocket*>(handle->data);
	buffer->base = socket->m_recvBuffer;
	buffer->len = kRecvBufferSize;
}

void TcpSocket::OnRead(uv_stream_t* stream, ssize_t bytesRead, const uv_buf_t* buffer) {
	auto* socket = static_cast<TcpSocket*>(stream->data);

	if (socket->IsDeleted()) {
		return;
	}

	if (bytesRead > 0) {
		RemoteEndpoint endpoint;
		if (socket->m_remoteEndpointSet.load(std::memory_order_acquire)) {
			std::atomic_thread_fence(std::memory_order_acquire);
			endpoint = socket->m_remoteEndpoint;
		}
		g_CallbackManager.EnqueueReceive(socket, buffer->base, bytesRead, endpoint);
	} else if (bytesRead < 0) {
		if (bytesRead == UV_EOF || bytesRead == UV_ECONNRESET || bytesRead == UV_ECONNABORTED) {
			g_CallbackManager.EnqueueDisconnect(socket);
		} else if (bytesRead != UV_ECANCELED) {
			g_CallbackManager.EnqueueError(socket, SocketError::RecvError, uv_strerror(static_cast<int>(bytesRead)));
		}
	}
}

bool TcpSocket::Send(std::string_view data, bool async) {
	auto* context = new TcpWriteContext;
	context->buffer = std::make_unique<char[]>(data.length());
	std::memcpy(context->buffer.get(), data.data(), data.length());
	context->length = data.length();
	context->socket = this;
	context->writeRequest.data = context;

	g_EventLoop.Post([this, context]() {
		if (IsDeleted()) {
			delete context;
			return;
		}

		uv_tcp_t* socket = m_socket.load(std::memory_order_acquire);
		if (!socket) {
			delete context;
			return;
		}

		uv_buf_t uvBuffer = uv_buf_init(context->buffer.get(), static_cast<unsigned int>(context->length));
		int result = uv_write(&context->writeRequest, reinterpret_cast<uv_stream_t*>(socket), &uvBuffer, 1, OnWrite);

		if (result != 0) {
			g_CallbackManager.EnqueueError(this, SocketError::SendError, uv_strerror(result));
			delete context;
		}
	});

	return true;
}

void TcpSocket::OnWrite(uv_write_t* request, int status) {
	auto* context = static_cast<TcpWriteContext*>(request->data);
	auto* socket = context->socket;

	if (!socket->IsDeleted() && status != 0 && status != UV_ECANCELED) {
		g_CallbackManager.EnqueueError(socket, SocketError::SendError, uv_strerror(status));
	}

	delete context;
}

bool TcpSocket::SendTo(std::string_view data, const char* hostname, uint16_t port, bool async) {
	return false;
}

bool TcpSocket::SetOption(SocketOption option, int value) {
	// Store in atomic array for immediate reads
	StoreOption(option, value);

	if (option == SocketOption::ConnectTimeout || option == SocketOption::AutoFreeHandle) {
		return true;
	}

	uv_tcp_t* socket = m_socket.load(std::memory_order_acquire);
	if (socket) {
		uv_os_sock_t socketFd;
		if (uv_fileno(reinterpret_cast<uv_handle_t*>(socket), reinterpret_cast<uv_os_fd_t*>(&socketFd)) == 0) {
			return SetSocketOption(socketFd, option, value);
		}
	}

	QueueOption(option, value);
	return true;
}

RemoteEndpoint TcpSocket::GetRemoteEndpoint() const {
	if (m_remoteEndpointSet.load(std::memory_order_acquire)) {
		std::atomic_thread_fence(std::memory_order_acquire);
		return m_remoteEndpoint;
	}
	return {};
}

RemoteEndpoint TcpSocket::GetLocalEndpoint() const {
	uv_tcp_t* socket = m_socket.load(std::memory_order_acquire);
	uv_tcp_t* acceptor = m_acceptor.load(std::memory_order_acquire);

	uv_tcp_t* handle = socket ? socket : acceptor;
	if (!handle) return {};

	sockaddr_storage addr;
	int addrLen = sizeof(addr);
	if (uv_tcp_getsockname(handle, reinterpret_cast<sockaddr*>(&addr), &addrLen) != 0) {
		return {};
	}
	return ExtractEndpoint(reinterpret_cast<sockaddr*>(&addr));
}

void TcpSocket::OnClose(uv_handle_t* handle) {
	if (handle->type == UV_TCP) {
		delete reinterpret_cast<uv_tcp_t*>(handle);
	}
}

void TcpSocket::OnShutdown(uv_shutdown_t* request, int status) {
	delete request;
}

void TcpSocket::OnConnectTimeout(uv_timer_t* timer) {
	auto* socket = static_cast<TcpSocket*>(timer->data);

	uv_tcp_t* socketToClose = socket->m_socket.exchange(nullptr, std::memory_order_acq_rel);

	if (socketToClose && !uv_is_closing(reinterpret_cast<uv_handle_t*>(socketToClose))) {
		uv_close(reinterpret_cast<uv_handle_t*>(socketToClose), OnClose);
	}

	if (!socket->IsDeleted()) {
		g_CallbackManager.EnqueueError(socket, SocketError::ConnectError, "Connection timed out");
	}

	uv_close(reinterpret_cast<uv_handle_t*>(timer), [](uv_handle_t* handle) {
		delete reinterpret_cast<uv_timer_t*>(handle);
	});
	socket->m_connectTimer = nullptr;
}

void TcpSocket::CancelConnectTimeout() {
	if (m_connectTimer) {
		uv_timer_stop(m_connectTimer);
		uv_close(reinterpret_cast<uv_handle_t*>(m_connectTimer), [](uv_handle_t* handle) {
			delete reinterpret_cast<uv_timer_t*>(handle);
		});
		m_connectTimer = nullptr;
	}
}
