#include "socket/TcpSocket.h"
#include "core/EventLoop.h"
#include "core/CallbackManager.h"
#include "core/SocketManager.h"
#include <cstring>

using SharedLock = std::shared_lock<std::shared_mutex>;

struct TcpConnectContext {
	uv_connect_t connectRequest;
	uv_getaddrinfo_t resolverRequest;
	TcpSocket* socket;
	std::unique_ptr<SharedLock> handlerLock;
};

struct TcpWriteContext {
	uv_write_t writeRequest;
	std::unique_ptr<char[]> buffer;
	size_t length;
	TcpSocket* socket;
	std::unique_ptr<SharedLock> handlerLock;
};

TcpSocket::TcpSocket() : SocketBase(SocketType::Tcp) {}

TcpSocket::~TcpSocket() {
	Disconnect();

	std::unique_lock<std::shared_mutex> lock(m_handlerMutex);
}

void TcpSocket::InitSocket() {
	if (m_socket) return;

	std::scoped_lock lock(m_socketMutex);
	if (m_socket) return;

	m_socket = new uv_tcp_t;
	uv_tcp_init(g_EventLoop.GetLoop(), m_socket);
	m_socket->data = this;

	if (m_localAddrSet) {
		uv_tcp_bind(m_socket, reinterpret_cast<const sockaddr*>(&m_localAddr), 0);
	}

	ApplyPendingOptions();
}

void TcpSocket::ApplyPendingOptions() {
	std::scoped_lock optionsLock(m_optionsMutex);

	uv_os_sock_t socketFd;
	if (m_socket && uv_fileno(reinterpret_cast<uv_handle_t*>(m_socket), reinterpret_cast<uv_os_fd_t*>(&socketFd)) == 0) {
		while (!m_pendingOptions.empty()) {
			auto& option = m_pendingOptions.front();
			SetSocketOption(socketFd, option.option, option.value);
			m_pendingOptions.pop();
		}
	}
}

bool TcpSocket::IsOpen() const {
	std::scoped_lock lock(m_socketMutex);
	return m_socket != nullptr && uv_is_active(reinterpret_cast<uv_handle_t*>(m_socket));
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

				if (status == 0 && addressInfo) {
					std::memcpy(&socket->m_localAddr, addressInfo->ai_addr, addressInfo->ai_addrlen);
					socket->m_localAddrSet = true;
					uv_freeaddrinfo(addressInfo);
				} else {
					g_CallbackManager.Enqueue(CallbackEvent::Error, socket,
						SocketError::BindError, status);
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
	context->handlerLock = std::make_unique<SharedLock>(m_handlerMutex);
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

	if (status != 0 || !addressInfo) {
		g_CallbackManager.Enqueue(CallbackEvent::Error, socket,
			SocketError::ConnectError, status);
		delete context;
		if (addressInfo) uv_freeaddrinfo(addressInfo);
		return;
	}

	if (!socket->m_socket) {
		socket->InitSocket();
	}

	if (addressInfo->ai_family == AF_INET || addressInfo->ai_family == AF_INET6) {
		std::scoped_lock endpointLock(socket->m_endpointMutex);
		socket->m_remoteEndpoint = ExtractEndpoint(addressInfo->ai_addr);
	}

	context->connectRequest.data = context;

	std::scoped_lock lock(socket->m_socketMutex);
	if (!socket->m_socket) {
		g_CallbackManager.Enqueue(CallbackEvent::Error, socket,
			SocketError::ConnectError, UV_ECANCELED);
		delete context;
		uv_freeaddrinfo(addressInfo);
		return;
	}

	int result = uv_tcp_connect(&context->connectRequest, socket->m_socket, addressInfo->ai_addr, OnConnect);
	uv_freeaddrinfo(addressInfo);

	if (result != 0) {
		g_CallbackManager.Enqueue(CallbackEvent::Error, socket,
			SocketError::ConnectError, result);
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

	if (status == 0) {
		g_CallbackManager.Enqueue(CallbackEvent::Connect, socket);
		socket->StartReceiving();
	} else if (status != UV_ECANCELED) {
		g_CallbackManager.Enqueue(CallbackEvent::Error, socket,
			SocketError::ConnectError, status);
	}

	delete context;
}

bool TcpSocket::Disconnect() {
	uv_tcp_t* socketToClose = nullptr;
	uv_tcp_t* acceptorToClose = nullptr;

	{
		std::scoped_lock lock(m_socketMutex);
		if (m_socket) {
			socketToClose = m_socket;
			m_socket = nullptr;
		}
	}

	{
		std::scoped_lock lock(m_acceptorMutex);
		if (m_acceptor) {
			acceptorToClose = m_acceptor;
			m_acceptor = nullptr;
		}
	}

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
	uv_tcp_t* socketToClose = nullptr;

	{
		std::scoped_lock lock(m_socketMutex);
		if (m_socket) {
			socketToClose = m_socket;
			m_socket = nullptr;
		}
	}

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
		{
			std::scoped_lock lock(m_acceptorMutex);

			if (!m_acceptor) {
				m_acceptor = new uv_tcp_t;
				uv_tcp_init(g_EventLoop.GetLoop(), m_acceptor);
				m_acceptor->data = this;

				int result = uv_tcp_bind(m_acceptor, reinterpret_cast<const sockaddr*>(&m_localAddr), 0);
				if (result != 0) {
					g_CallbackManager.Enqueue(CallbackEvent::Error, this, SocketError::BindError, result);
					uv_close(reinterpret_cast<uv_handle_t*>(m_acceptor), OnClose);
					m_acceptor = nullptr;
					return;
				}

				uv_os_sock_t socketFd;
				if (uv_fileno(reinterpret_cast<uv_handle_t*>(m_acceptor), reinterpret_cast<uv_os_fd_t*>(&socketFd)) == 0) {
					std::scoped_lock optionsLock(m_optionsMutex);
					while (!m_pendingOptions.empty()) {
						auto& option = m_pendingOptions.front();
						SetSocketOption(socketFd, option.option, option.value);
						m_pendingOptions.pop();
					}
				}
			}

			int result = uv_listen(reinterpret_cast<uv_stream_t*>(m_acceptor), SOMAXCONN, OnConnection);
			if (result != 0) {
				g_CallbackManager.Enqueue(CallbackEvent::Error, this, SocketError::ListenError, result);
				return;
			}
		}
		g_CallbackManager.Enqueue(CallbackEvent::Listen, this, GetLocalEndpoint());
	});

	return true;
}

void TcpSocket::OnConnection(uv_stream_t* server, int status) {
	auto* socket = static_cast<TcpSocket*>(server->data);

	if (status < 0) {
		if (status != UV_ECANCELED) {
			g_CallbackManager.Enqueue(CallbackEvent::Error, socket,
				SocketError::ListenError, status);
		}
		return;
	}

	auto* clientHandle = new uv_tcp_t;
	uv_tcp_init(g_EventLoop.GetLoop(), clientHandle);

	if (uv_accept(server, reinterpret_cast<uv_stream_t*>(clientHandle)) == 0) {
		TcpSocket* newSocket = TcpSocket::CreateFromAccepted(clientHandle);
		if (newSocket) {
			g_CallbackManager.Enqueue(CallbackEvent::Incoming, socket, newSocket);
			newSocket->StartReceiving();
		}
	} else {
		uv_close(reinterpret_cast<uv_handle_t*>(clientHandle), OnClose);
	}
}

TcpSocket* TcpSocket::CreateFromAccepted(uv_tcp_t* clientHandle) {
	auto* socket = g_SocketManager.CreateSocket<TcpSocket>();
	socket->m_socket = clientHandle;
	clientHandle->data = socket;

	sockaddr_storage peerAddress;
	int addressLength = sizeof(peerAddress);
	if (uv_tcp_getpeername(clientHandle, reinterpret_cast<sockaddr*>(&peerAddress), &addressLength) == 0) {
		socket->m_remoteEndpoint = ExtractEndpoint(reinterpret_cast<sockaddr*>(&peerAddress));
	}

	return socket;
}

void TcpSocket::StartReceiving() {
	std::scoped_lock lock(m_socketMutex);
	if (!m_socket) return;

	uv_read_start(reinterpret_cast<uv_stream_t*>(m_socket), OnAllocBuffer, OnRead);
}

void TcpSocket::OnAllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buffer) {
	buffer->base = new char[kRecvBufferSize];
	buffer->len = kRecvBufferSize;
}

void TcpSocket::OnRead(uv_stream_t* stream, ssize_t bytesRead, const uv_buf_t* buffer) {
	auto* socket = static_cast<TcpSocket*>(stream->data);

	if (bytesRead > 0) {
		g_CallbackManager.Enqueue(CallbackEvent::Receive, socket, buffer->base, bytesRead, socket->GetRemoteEndpoint());
	} else if (bytesRead < 0) {
		if (bytesRead == UV_EOF || bytesRead == UV_ECONNRESET || bytesRead == UV_ECONNABORTED) {
			g_CallbackManager.Enqueue(CallbackEvent::Disconnect, socket);
		} else if (bytesRead != UV_ECANCELED) {
			g_CallbackManager.Enqueue(CallbackEvent::Error, socket,
				SocketError::RecvError, static_cast<int>(bytesRead));
		}
	}

	delete[] buffer->base;
}

bool TcpSocket::Send(std::string_view data, bool async) {
	auto* context = new TcpWriteContext;
	context->buffer = std::make_unique<char[]>(data.length());
	std::memcpy(context->buffer.get(), data.data(), data.length());
	context->length = data.length();
	context->socket = this;
	context->handlerLock = std::make_unique<SharedLock>(m_handlerMutex);
	context->writeRequest.data = context;

	g_EventLoop.Post([this, context]() {
		std::scoped_lock lock(m_socketMutex);
		if (!m_socket) {
			delete context;
			return;
		}

		uv_buf_t uvBuffer = uv_buf_init(context->buffer.get(), static_cast<unsigned int>(context->length));
		int result = uv_write(&context->writeRequest, reinterpret_cast<uv_stream_t*>(m_socket), &uvBuffer, 1, OnWrite);

		if (result != 0) {
			g_CallbackManager.Enqueue(CallbackEvent::Error, this, SocketError::SendError, result);
			delete context;
		}
	});

	return true;
}

void TcpSocket::OnWrite(uv_write_t* request, int status) {
	auto* context = static_cast<TcpWriteContext*>(request->data);
	auto* socket = context->socket;

	if (status != 0 && status != UV_ECANCELED) {
		g_CallbackManager.Enqueue(CallbackEvent::Error, socket,
			SocketError::SendError, status);
	}

	delete context;
}

bool TcpSocket::SendTo(std::string_view data, const char* hostname, uint16_t port, bool async) {
	return false;
}

bool TcpSocket::SetOption(SocketOption option, int value) {
	if (option == SocketOption::ConnectTimeout || option == SocketOption::AutoClose) {
		std::scoped_lock lock(m_optionsMutex);
		m_options[option] = value;
		return true;
	}

	std::scoped_lock lock(m_socketMutex);

	if (m_socket) {
		uv_os_sock_t socketFd;
		if (uv_fileno(reinterpret_cast<uv_handle_t*>(m_socket), reinterpret_cast<uv_os_fd_t*>(&socketFd)) == 0) {
			return SetSocketOption(socketFd, option, value);
		}
	}

	QueueOption(option, value);
	return true;
}

RemoteEndpoint TcpSocket::GetRemoteEndpoint() const {
	std::scoped_lock lock(m_endpointMutex);
	return m_remoteEndpoint;
}

RemoteEndpoint TcpSocket::GetLocalEndpoint() const {
	std::scoped_lock lock(m_socketMutex, m_acceptorMutex);

	uv_tcp_t* handle = m_socket ? m_socket : m_acceptor;
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

	uv_tcp_t* socketToClose = nullptr;
	{
		std::scoped_lock lock(socket->m_socketMutex);
		if (socket->m_socket) {
			socketToClose = socket->m_socket;
			socket->m_socket = nullptr;
		}
	}

	if (socketToClose && !uv_is_closing(reinterpret_cast<uv_handle_t*>(socketToClose))) {
		uv_close(reinterpret_cast<uv_handle_t*>(socketToClose), OnClose);
	}

	g_CallbackManager.Enqueue(CallbackEvent::Error, socket, SocketError::ConnectError, UV_ETIMEDOUT);

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