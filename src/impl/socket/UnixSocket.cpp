#ifndef _WIN32

#include "socket/UnixSocket.h"
#include "core/EventLoop.h"
#include "core/CallbackManager.h"
#include "core/SocketManager.h"
#include <cstring>
#include <atomic>

struct UnixWriteContext {
	uv_write_t writeRequest;
	std::unique_ptr<char[]> buffer;
	size_t length;
	UnixSocket* socket;
};

UnixSocket::UnixSocket() : SocketBase(SocketType::Unix) {}

UnixSocket::~UnixSocket() {
	Disconnect();
}

void UnixSocket::InitPipe() {
	uv_pipe_t* expected = nullptr;
	uv_pipe_t* newPipe = new uv_pipe_t;
	uv_pipe_init(g_EventLoop.GetLoop(), newPipe, 0);
	newPipe->data = this;

	if (!m_pipe.compare_exchange_strong(expected, newPipe,
		std::memory_order_release, std::memory_order_acquire)) {
		uv_close(reinterpret_cast<uv_handle_t*>(newPipe), OnClose);
	}
}

bool UnixSocket::IsOpen() const {
	return m_pipe.load(std::memory_order_acquire) != nullptr ||
	       m_acceptor.load(std::memory_order_acquire) != nullptr;
}

bool UnixSocket::Bind(const char* path, uint16_t port, bool async) {
	if (m_path.empty()) {
		m_path = path;
	}
	return true;
}

bool UnixSocket::Connect(const char* path, uint16_t port, bool async) {
	m_path = path;

	g_EventLoop.Post([this]() {
		if (IsDeleted()) return;

		if (m_pipe.load(std::memory_order_acquire) == nullptr) {
			InitPipe();
		}

		uv_pipe_t* pipe = m_pipe.load(std::memory_order_acquire);
		if (!pipe) return;

		auto* connectReq = new uv_connect_t;
		connectReq->data = this;

		uv_pipe_connect(connectReq, pipe, m_path.c_str(), OnConnect);
	});

	return true;
}

void UnixSocket::OnConnect(uv_connect_t* request, int status) {
	auto* socket = static_cast<UnixSocket*>(request->data);
	delete request;

	if (socket->IsDeleted()) return;

	if (status == 0) {
		RemoteEndpoint endpoint;
		endpoint.address = socket->m_path;
		g_CallbackManager.EnqueueConnect(socket, endpoint);
		socket->StartReading();
	} else {
		g_CallbackManager.EnqueueError(socket, SocketError::ConnectError, uv_strerror(status));
	}
}

bool UnixSocket::Disconnect() {
	uv_pipe_t* pipeToClose = m_pipe.exchange(nullptr, std::memory_order_acq_rel);
	uv_pipe_t* acceptorToClose = m_acceptor.exchange(nullptr, std::memory_order_acq_rel);

	if (pipeToClose) {
		g_EventLoop.Post([pipeToClose]() {
			if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(pipeToClose))) {
				uv_read_stop(reinterpret_cast<uv_stream_t*>(pipeToClose));
				uv_close(reinterpret_cast<uv_handle_t*>(pipeToClose), OnClose);
			}
		});
	}

	if (acceptorToClose) {
		g_EventLoop.Post([acceptorToClose]() {
			if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(acceptorToClose))) {
				uv_close(reinterpret_cast<uv_handle_t*>(acceptorToClose), OnClose);
			}
		});
	}

	return true;
}

bool UnixSocket::CloseReset() {
	return Disconnect();
}

bool UnixSocket::Listen() {
	if (m_path.empty()) {
		return false;
	}

	g_EventLoop.Post([this]() {
		if (IsDeleted()) return;

		uv_pipe_t* expected = nullptr;
		uv_pipe_t* acceptor = new uv_pipe_t;
		uv_pipe_init(g_EventLoop.GetLoop(), acceptor, 0);
		acceptor->data = this;

		if (!m_acceptor.compare_exchange_strong(expected, acceptor,
			std::memory_order_release, std::memory_order_acquire)) {
			uv_close(reinterpret_cast<uv_handle_t*>(acceptor), OnClose);
			return;
		}

		int result = uv_pipe_bind(acceptor, m_path.c_str());
		if (result != 0) {
			g_CallbackManager.EnqueueError(this, SocketError::BindError, uv_strerror(result));
			m_acceptor.store(nullptr, std::memory_order_release);
			uv_close(reinterpret_cast<uv_handle_t*>(acceptor), OnClose);
			return;
		}

		result = uv_listen(reinterpret_cast<uv_stream_t*>(acceptor), 128, OnConnection);
		if (result != 0) {
			g_CallbackManager.EnqueueError(this, SocketError::ListenError, uv_strerror(result));
			m_acceptor.store(nullptr, std::memory_order_release);
			uv_close(reinterpret_cast<uv_handle_t*>(acceptor), OnClose);
			return;
		}

		RemoteEndpoint localEndpoint;
		localEndpoint.address = m_path;
		g_CallbackManager.EnqueueListen(this, localEndpoint);
	});

	return true;
}

void UnixSocket::OnConnection(uv_stream_t* server, int status) {
	auto* socket = static_cast<UnixSocket*>(server->data);

	if (socket->IsDeleted()) return;

	if (status < 0) {
		g_CallbackManager.EnqueueError(socket, SocketError::ListenError, uv_strerror(status));
		return;
	}

	uv_pipe_t* client = new uv_pipe_t;
	uv_pipe_init(g_EventLoop.GetLoop(), client, 0);

	if (uv_accept(server, reinterpret_cast<uv_stream_t*>(client)) == 0) {
		UnixSocket* newSocket = CreateFromAccepted(client, socket->m_path);
		if (newSocket) {
			RemoteEndpoint remoteEndpoint;
			remoteEndpoint.address = socket->m_path;
			g_CallbackManager.EnqueueIncoming(socket, newSocket, remoteEndpoint);
			newSocket->StartReading();
		}
	} else {
		uv_close(reinterpret_cast<uv_handle_t*>(client), OnClose);
	}
}

UnixSocket* UnixSocket::CreateFromAccepted(uv_pipe_t* clientHandle, const std::string& path) {
	auto* socket = g_SocketManager.CreateSocket<UnixSocket>();
	socket->m_path = path;
	socket->m_pipe.store(clientHandle, std::memory_order_release);
	clientHandle->data = socket;
	return socket;
}

bool UnixSocket::Send(std::string_view data, bool async) {
	uv_pipe_t* pipe = m_pipe.load(std::memory_order_acquire);
	if (!pipe) return false;

	auto* context = new UnixWriteContext;
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

		uv_pipe_t* pipe = m_pipe.load(std::memory_order_acquire);
		if (!pipe) {
			delete context;
			return;
		}

		uv_buf_t uvBuffer = uv_buf_init(context->buffer.get(), static_cast<unsigned int>(context->length));
		int result = uv_write(&context->writeRequest, reinterpret_cast<uv_stream_t*>(pipe), &uvBuffer, 1, OnWrite);

		if (result != 0) {
			g_CallbackManager.EnqueueError(this, SocketError::SendError, uv_strerror(result));
			delete context;
		}
	});

	return true;
}

bool UnixSocket::SendTo(std::string_view data, const char* hostname, uint16_t port, bool async) {
	// Unix sockets don't support SendTo, use Send instead
	return Send(data, async);
}

void UnixSocket::OnWrite(uv_write_t* request, int status) {
	auto* context = static_cast<UnixWriteContext*>(request->data);
	auto* socket = context->socket;

	if (!socket->IsDeleted() && status != 0 && status != UV_ECANCELED) {
		g_CallbackManager.EnqueueError(socket, SocketError::SendError, uv_strerror(status));
	}

	delete context;
}

void UnixSocket::StartReading() {
	uv_pipe_t* pipe = m_pipe.load(std::memory_order_acquire);
	if (!pipe) return;

	uv_read_start(reinterpret_cast<uv_stream_t*>(pipe), OnAllocBuffer, OnRead);
}

void UnixSocket::OnAllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buffer) {
	auto* socket = static_cast<UnixSocket*>(handle->data);
	buffer->base = socket->m_recvBuffer;
	buffer->len = kRecvBufferSize;
}

void UnixSocket::OnRead(uv_stream_t* stream, ssize_t bytesRead, const uv_buf_t* buffer) {
	auto* socket = static_cast<UnixSocket*>(stream->data);

	if (socket->IsDeleted()) {
		return;
	}

	if (bytesRead > 0) {
		g_CallbackManager.EnqueueReceive(socket, buffer->base, bytesRead);
	} else if (bytesRead < 0) {
		if (bytesRead == UV_EOF) {
			g_CallbackManager.EnqueueDisconnect(socket);
		} else if (bytesRead != UV_ECANCELED) {
			g_CallbackManager.EnqueueError(socket, SocketError::RecvError, uv_strerror(static_cast<int>(bytesRead)));
		}
	}
}

bool UnixSocket::SetOption(SocketOption option, int value) {
	StoreOption(option, value);
	return true;
}

void UnixSocket::OnClose(uv_handle_t* handle) {
	if (handle->type == UV_NAMED_PIPE) {
		delete reinterpret_cast<uv_pipe_t*>(handle);
	}
}

#endif // _WIN32
