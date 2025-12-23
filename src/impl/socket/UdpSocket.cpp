#include "socket/UdpSocket.h"
#include "core/EventLoop.h"
#include "core/CallbackManager.h"
#include <cstring>

using SharedLock = std::shared_lock<std::shared_mutex>;

struct UdpSendContext {
	uv_udp_send_t sendRequest;
	uv_getaddrinfo_t resolverRequest;
	std::unique_ptr<char[]> buffer;
	size_t length;
	UdpSocket* socket;
	std::unique_ptr<SharedLock> handlerLock;
};

UdpSocket::UdpSocket() : SocketBase(SocketType::Udp) {}

UdpSocket::~UdpSocket() {
	Disconnect();

	std::unique_lock<std::shared_mutex> lock(m_handlerMutex);
}

void UdpSocket::InitSocket(int addressFamily) {
	if (m_socket) return;

	std::scoped_lock lock(m_socketMutex);
	if (m_socket) return;

	m_socket = new uv_udp_t;
	uv_udp_init(g_EventLoop.GetLoop(), m_socket);
	m_socket->data = this;

	int result = 0;
	if (m_localAddrSet) {
		result = uv_udp_bind(m_socket, reinterpret_cast<const sockaddr*>(&m_localAddr), 0);
	} else if (addressFamily == AF_INET6) {
		sockaddr_in6 anyAddress{};
		anyAddress.sin6_family = AF_INET6;
		anyAddress.sin6_addr = in6addr_any;
		anyAddress.sin6_port = 0;
		result = uv_udp_bind(m_socket, reinterpret_cast<const sockaddr*>(&anyAddress), 0);
	} else {
		sockaddr_in anyAddress{};
		anyAddress.sin_family = AF_INET;
		anyAddress.sin_addr.s_addr = INADDR_ANY;
		anyAddress.sin_port = 0;
		result = uv_udp_bind(m_socket, reinterpret_cast<const sockaddr*>(&anyAddress), 0);
	}

	if (result != 0) {
		g_CallbackManager.Enqueue(CallbackEvent::Error, this, SocketError::BindError, result);
	}

	ApplyPendingOptions();
}

void UdpSocket::ApplyPendingOptions() {
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

bool UdpSocket::IsOpen() const {
	std::scoped_lock lock(m_socketMutex);
	return m_socket != nullptr;
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

			if (status == 0 && addressInfo) {
				if (!socket->m_socket) {
					socket->InitSocket(addressInfo->ai_family);
				}

				std::memcpy(&socket->m_connectedAddr, addressInfo->ai_addr, addressInfo->ai_addrlen);
				socket->m_isConnected = true;
				uv_freeaddrinfo(addressInfo);

				g_CallbackManager.Enqueue(CallbackEvent::Connect, socket);
				socket->StartReceiving();
			} else {
				g_CallbackManager.Enqueue(CallbackEvent::Error, socket,
					SocketError::ConnectError, status);
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
	uv_udp_t* socketToClose = nullptr;

	{
		std::scoped_lock lock(m_socketMutex);
		if (m_socket) {
			socketToClose = m_socket;
			m_socket = nullptr;
		}
	}

	m_isConnected = false;

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

bool UdpSocket::Listen() {
	if (!m_localAddrSet) {
		return false;
	}

	g_EventLoop.Post([this]() {
		if (!m_socket) {
			InitSocket();
		}
		StartReceiving();
		g_CallbackManager.Enqueue(CallbackEvent::Listen, this, GetLocalEndpoint());
	});

	return true;
}

bool UdpSocket::Send(std::string_view data, bool async) {
	if (!m_isConnected) {
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
		context->handlerLock = std::make_unique<SharedLock>(m_handlerMutex);
		context->resolverRequest.data = context;

		int result = uv_getaddrinfo(g_EventLoop.GetLoop(), &context->resolverRequest,
			[](uv_getaddrinfo_t* resolverRequest, int status, struct addrinfo* addressInfo) {
				auto* context = static_cast<UdpSendContext*>(resolverRequest->data);
				auto* socket = context->socket;

				if (status != 0 || !addressInfo) {
					g_CallbackManager.Enqueue(CallbackEvent::Error, socket,
						SocketError::NoHost, status);
					delete context;
					if (addressInfo) uv_freeaddrinfo(addressInfo);
					return;
				}

				if (!socket->m_socket) {
					socket->InitSocket(addressInfo->ai_family);
				}

				std::scoped_lock lock(socket->m_socketMutex);
				if (!socket->m_socket) {
					delete context;
					uv_freeaddrinfo(addressInfo);
					return;
				}

				context->sendRequest.data = context;
				uv_buf_t uvBuffer = uv_buf_init(context->buffer.get(), static_cast<unsigned int>(context->length));

				int sendResult = uv_udp_send(&context->sendRequest, socket->m_socket, &uvBuffer, 1, addressInfo->ai_addr, OnSend);
				uv_freeaddrinfo(addressInfo);

				if (sendResult != 0) {
					g_CallbackManager.Enqueue(CallbackEvent::Error, socket,
						SocketError::SendError, sendResult);
					delete context;
				}
			},
			hostname, portString, &hints);

		if (result != 0) {
			delete context;
			return false;
		}

		return true;
	} else if (m_isConnected) {
		auto* context = new UdpSendContext;
		context->buffer = std::make_unique<char[]>(data.length());
		std::memcpy(context->buffer.get(), data.data(), data.length());
		context->length = data.length();
		context->socket = this;
		context->handlerLock = std::make_unique<SharedLock>(m_handlerMutex);
		context->sendRequest.data = context;

		g_EventLoop.Post([this, context]() {
			std::scoped_lock lock(m_socketMutex);
			if (!m_socket) {
				delete context;
				return;
			}

			uv_buf_t uvBuffer = uv_buf_init(context->buffer.get(), static_cast<unsigned int>(context->length));
			const sockaddr* destinationAddress = reinterpret_cast<const sockaddr*>(&m_connectedAddr);
			int result = uv_udp_send(&context->sendRequest, m_socket, &uvBuffer, 1, destinationAddress, OnSend);

			if (result != 0) {
				g_CallbackManager.Enqueue(CallbackEvent::Error, this, SocketError::SendError, result);
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

	if (status != 0 && status != UV_ECANCELED) {
		g_CallbackManager.Enqueue(CallbackEvent::Error, socket,
			SocketError::SendError, status);
	}

	delete context;
}

void UdpSocket::StartReceiving() {
	std::scoped_lock lock(m_socketMutex);
	if (!m_socket) return;

	uv_udp_recv_start(m_socket, OnAllocBuffer, OnRecv);
}

void UdpSocket::OnAllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buffer) {
	buffer->base = new char[kRecvBufferSize];
	buffer->len = kRecvBufferSize;
}

void UdpSocket::OnRecv(uv_udp_t* handle, ssize_t bytesRead, const uv_buf_t* buffer,
					   const struct sockaddr* senderAddress, unsigned flags) {
	auto* socket = static_cast<UdpSocket*>(handle->data);

	if (bytesRead > 0) {
		RemoteEndpoint sender = ExtractEndpoint(senderAddress);
		g_CallbackManager.Enqueue(CallbackEvent::Receive, socket, buffer->base, bytesRead, sender);
	} else if (bytesRead < 0) {
		if (bytesRead == UV_EOF) {
			g_CallbackManager.Enqueue(CallbackEvent::Disconnect, socket);
		} else if (bytesRead != UV_ECANCELED) {
			g_CallbackManager.Enqueue(CallbackEvent::Error, socket,
				SocketError::RecvError, static_cast<int>(bytesRead));
		}
	}

	delete[] buffer->base;
}

bool UdpSocket::SetOption(SocketOption option, int value) {
	if (option == SocketOption::AutoClose) {
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

RemoteEndpoint UdpSocket::GetLocalEndpoint() const {
	std::scoped_lock lock(m_socketMutex);
	if (!m_socket) return {};

	sockaddr_storage addr;
	int addrLen = sizeof(addr);
	if (uv_udp_getsockname(m_socket, reinterpret_cast<sockaddr*>(&addr), &addrLen) != 0) {
		return {};
	}
	return ExtractEndpoint(reinterpret_cast<sockaddr*>(&addr));
}

void UdpSocket::OnClose(uv_handle_t* handle) {
	if (handle->type == UV_UDP) {
		delete reinterpret_cast<uv_udp_t*>(handle);
	}
}