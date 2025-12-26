#include "socket/UdpSocket.h"
#include "core/EventLoop.h"
#include "core/CallbackManager.h"
#include <cstring>
#include <atomic>

struct UdpSendContext {
	uv_udp_send_t sendRequest;
	uv_getaddrinfo_t resolverRequest;
	std::unique_ptr<char[]> buffer;
	size_t length;
	UdpSocket* socket;
};

UdpSocket::UdpSocket() : SocketBase(SocketType::Udp) {}

UdpSocket::~UdpSocket() {
	Disconnect();
}

void UdpSocket::InitSocket(int addressFamily) {
	uv_udp_t* expected = nullptr;
	uv_udp_t* newSocket = new uv_udp_t;
	uv_udp_init(g_EventLoop.GetLoop(), newSocket);
	newSocket->data = this;

	if (!m_socket.compare_exchange_strong(expected, newSocket,
		std::memory_order_release, std::memory_order_acquire)) {
		// Another thread already initialized the socket
		uv_close(reinterpret_cast<uv_handle_t*>(newSocket), OnClose);
		return;
	}

	int result = 0;
	if (m_localAddrSet) {
		result = uv_udp_bind(newSocket, reinterpret_cast<const sockaddr*>(&m_localAddr), 0);
	} else if (addressFamily == AF_INET6) {
		sockaddr_in6 anyAddress{};
		anyAddress.sin6_family = AF_INET6;
		anyAddress.sin6_addr = in6addr_any;
		anyAddress.sin6_port = 0;
		result = uv_udp_bind(newSocket, reinterpret_cast<const sockaddr*>(&anyAddress), 0);
	} else {
		sockaddr_in anyAddress{};
		anyAddress.sin_family = AF_INET;
		anyAddress.sin_addr.s_addr = INADDR_ANY;
		anyAddress.sin_port = 0;
		result = uv_udp_bind(newSocket, reinterpret_cast<const sockaddr*>(&anyAddress), 0);
	}

	if (result != 0) {
		g_CallbackManager.EnqueueError(this, SocketError::BindError, uv_strerror(result));
	}

	ApplyPendingOptions(reinterpret_cast<uv_handle_t*>(newSocket));
}

bool UdpSocket::IsOpen() const {
	return m_socket.load(std::memory_order_acquire) != nullptr;
}

bool UdpSocket::Bind(const char* hostname, uint16_t port, bool async) {
	if (m_localAddrSet) {
		return false;
	}

	struct addrinfo hints{};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;

	char portString[6];
	snprintf(portString, sizeof(portString), "%hu", port);

	if (async) {
		auto* request = new uv_getaddrinfo_t;
		request->data = this;

		int result = uv_getaddrinfo(g_EventLoop.GetLoop(), request,
			[](uv_getaddrinfo_t* request, int status, struct addrinfo* addressInfo) {
				auto* socket = static_cast<UdpSocket*>(request->data);

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

bool UdpSocket::Connect(const char* hostname, uint16_t port, bool async) {
	char portString[6];
	snprintf(portString, sizeof(portString), "%hu", port);

	struct addrinfo hints{};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;

	auto* request = new uv_getaddrinfo_t;
	request->data = this;

	int result = uv_getaddrinfo(g_EventLoop.GetLoop(), request,
		[](uv_getaddrinfo_t* request, int status, struct addrinfo* addressInfo) {
			auto* socket = static_cast<UdpSocket*>(request->data);

			if (socket->IsDeleted()) {
				if (addressInfo) uv_freeaddrinfo(addressInfo);
				delete request;
				return;
			}

			if (status == 0 && addressInfo) {
				if (socket->m_socket.load(std::memory_order_acquire) == nullptr) {
					socket->InitSocket(addressInfo->ai_family);
				}

				std::memcpy(&socket->m_connectedAddr, addressInfo->ai_addr, addressInfo->ai_addrlen);
				socket->m_isConnected.store(true, std::memory_order_release);
				uv_freeaddrinfo(addressInfo);

				RemoteEndpoint endpoint;
				g_CallbackManager.EnqueueConnect(socket, endpoint);
				socket->StartReceiving();
			} else {
				g_CallbackManager.EnqueueError(socket, SocketError::ConnectError, uv_strerror(status));
			}

			delete request;
		},
		hostname, portString, &hints);

	if (result != 0) {
		delete request;
		return false;
	}

	return true;
}

bool UdpSocket::Disconnect() {
	uv_udp_t* socketToClose = m_socket.exchange(nullptr, std::memory_order_acq_rel);
	m_isConnected.store(false, std::memory_order_release);

	if (socketToClose) {
		g_EventLoop.Post([socketToClose]() {
			if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(socketToClose))) {
				uv_udp_recv_stop(socketToClose);
				uv_close(reinterpret_cast<uv_handle_t*>(socketToClose), OnClose);
			}
		});
	}

	return true;
}

bool UdpSocket::CloseReset() {
	// UDP doesn't have RST, just do normal disconnect
	return Disconnect();
}

bool UdpSocket::Listen() {
	if (!m_localAddrSet) {
		return false;
	}

	g_EventLoop.Post([this]() {
		if (IsDeleted()) return;

		if (m_socket.load(std::memory_order_acquire) == nullptr) {
			InitSocket();
		}
		StartReceiving();
		g_CallbackManager.EnqueueListen(this, GetLocalEndpoint());
	});

	return true;
}

bool UdpSocket::Send(std::string_view data, bool async) {
	if (!m_isConnected.load(std::memory_order_acquire)) {
		return false;
	}

	return SendTo(data, nullptr, 0, async);
}

bool UdpSocket::SendTo(std::string_view data, const char* hostname, uint16_t port, bool async) {
	if (hostname && port > 0) {
		char portString[6];
		snprintf(portString, sizeof(portString), "%hu", port);

		struct addrinfo hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = IPPROTO_UDP;

		auto* context = new UdpSendContext;
		context->buffer = std::make_unique<char[]>(data.length());
		std::memcpy(context->buffer.get(), data.data(), data.length());
		context->length = data.length();
		context->socket = this;
		context->resolverRequest.data = context;

		int result = uv_getaddrinfo(g_EventLoop.GetLoop(), &context->resolverRequest,
			[](uv_getaddrinfo_t* resolverRequest, int status, struct addrinfo* addressInfo) {
				auto* context = static_cast<UdpSendContext*>(resolverRequest->data);
				auto* socket = context->socket;

				if (socket->IsDeleted()) {
					delete context;
					if (addressInfo) uv_freeaddrinfo(addressInfo);
					return;
				}

				if (status != 0 || !addressInfo) {
					g_CallbackManager.EnqueueError(socket, SocketError::NoHost, uv_strerror(status));
					delete context;
					if (addressInfo) uv_freeaddrinfo(addressInfo);
					return;
				}

				if (socket->m_socket.load(std::memory_order_acquire) == nullptr) {
					socket->InitSocket(addressInfo->ai_family);
				}

				uv_udp_t* udpSocket = socket->m_socket.load(std::memory_order_acquire);
				if (!udpSocket) {
					delete context;
					uv_freeaddrinfo(addressInfo);
					return;
				}

				context->sendRequest.data = context;
				uv_buf_t uvBuffer = uv_buf_init(context->buffer.get(), static_cast<unsigned int>(context->length));

				int sendResult = uv_udp_send(&context->sendRequest, udpSocket, &uvBuffer, 1, addressInfo->ai_addr, OnSend);
				uv_freeaddrinfo(addressInfo);

				if (sendResult != 0) {
					g_CallbackManager.EnqueueError(socket, SocketError::SendError, uv_strerror(sendResult));
					delete context;
				}
			},
			hostname, portString, &hints);

		if (result != 0) {
			delete context;
			return false;
		}

		return true;
	} else if (m_isConnected.load(std::memory_order_acquire)) {
		auto* context = new UdpSendContext;
		context->buffer = std::make_unique<char[]>(data.length());
		std::memcpy(context->buffer.get(), data.data(), data.length());
		context->length = data.length();
		context->socket = this;
		context->sendRequest.data = context;

		g_EventLoop.Post([this, context]() {
			if (IsDeleted()) {
				delete context;
				return;
			}

			uv_udp_t* socket = m_socket.load(std::memory_order_acquire);
			if (!socket) {
				delete context;
				return;
			}

			uv_buf_t uvBuffer = uv_buf_init(context->buffer.get(), static_cast<unsigned int>(context->length));
			const sockaddr* destinationAddress = reinterpret_cast<const sockaddr*>(&m_connectedAddr);
			int result = uv_udp_send(&context->sendRequest, socket, &uvBuffer, 1, destinationAddress, OnSend);

			if (result != 0) {
				g_CallbackManager.EnqueueError(this, SocketError::SendError, uv_strerror(result));
				delete context;
			}
		});

		return true;
	}

	return false;
}

void UdpSocket::OnSend(uv_udp_send_t* request, int status) {
	auto* context = static_cast<UdpSendContext*>(request->data);
	auto* socket = context->socket;

	if (!socket->IsDeleted() && status != 0 && status != UV_ECANCELED) {
		g_CallbackManager.EnqueueError(socket, SocketError::SendError, uv_strerror(status));
	}

	delete context;
}

void UdpSocket::StartReceiving() {
	uv_udp_t* socket = m_socket.load(std::memory_order_acquire);
	if (!socket) return;

	uv_udp_recv_start(socket, OnAllocBuffer, OnRecv);
}

void UdpSocket::OnAllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buffer) {
	auto* socket = static_cast<UdpSocket*>(handle->data);
	buffer->base = socket->m_recvBuffer;
	buffer->len = kRecvBufferSize;
}

void UdpSocket::OnRecv(uv_udp_t* handle, ssize_t bytesRead, const uv_buf_t* buffer,
					   const struct sockaddr* senderAddress, unsigned flags) {
	auto* socket = static_cast<UdpSocket*>(handle->data);

	if (socket->IsDeleted()) {
		return;
	}

	if (bytesRead > 0) {
		RemoteEndpoint sender = ExtractEndpoint(senderAddress);
		g_CallbackManager.EnqueueReceive(socket, buffer->base, bytesRead, sender);
	} else if (bytesRead < 0) {
		if (bytesRead == UV_EOF) {
			g_CallbackManager.EnqueueDisconnect(socket);
		} else if (bytesRead != UV_ECANCELED) {
			g_CallbackManager.EnqueueError(socket, SocketError::RecvError, uv_strerror(static_cast<int>(bytesRead)));
		}
	}
}

bool UdpSocket::SetOption(SocketOption option, int value) {
	// Store in atomic array for immediate reads
	StoreOption(option, value);

	if (option == SocketOption::AutoFreeHandle) {
		return true;
	}

	uv_udp_t* socket = m_socket.load(std::memory_order_acquire);
	if (socket) {
		uv_os_sock_t socketFd;
		if (uv_fileno(reinterpret_cast<uv_handle_t*>(socket), reinterpret_cast<uv_os_fd_t*>(&socketFd)) == 0) {
			return SetSocketOption(socketFd, option, value);
		}
	}

	QueueOption(option, value);
	return true;
}

RemoteEndpoint UdpSocket::GetLocalEndpoint() const {
	uv_udp_t* socket = m_socket.load(std::memory_order_acquire);
	if (!socket) return {};

	sockaddr_storage addr;
	int addrLen = sizeof(addr);
	if (uv_udp_getsockname(socket, reinterpret_cast<sockaddr*>(&addr), &addrLen) != 0) {
		return {};
	}
	return ExtractEndpoint(reinterpret_cast<sockaddr*>(&addr));
}

void UdpSocket::OnClose(uv_handle_t* handle) {
	if (handle->type == UV_UDP) {
		delete reinterpret_cast<uv_udp_t*>(handle);
	}
}
