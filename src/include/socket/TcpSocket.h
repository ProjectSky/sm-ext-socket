#pragma once

#include "socket/SocketBase.h"
#include <uv.h>
#include <memory>
#include <functional>

class TcpSocket;

class TcpSocket : public SocketBase {
public:
	TcpSocket();
	~TcpSocket() override;

	[[nodiscard]] bool IsOpen() const override;
	bool Bind(const char* hostname, uint16_t port, bool async = true) override;
	bool Connect(const char* hostname, uint16_t port, bool async = true) override;
	bool Disconnect() override;
	bool CloseReset() override;
	bool Listen() override;
	bool Send(std::string_view data, bool async = true) override;
	bool SendTo(std::string_view data, const char* hostname, uint16_t port, bool async = true) override;
	bool SetOption(SocketOption option, int value) override;

	static TcpSocket* CreateFromAccepted(uv_tcp_t* client);

	[[nodiscard]] RemoteEndpoint GetRemoteEndpoint() const;
	[[nodiscard]] RemoteEndpoint GetLocalEndpoint() const;

private:
	void InitSocket();

	void ApplyPendingOptions();

	static void OnResolved(uv_getaddrinfo_t* request, int status, struct addrinfo* addressInfo);
	static void OnConnect(uv_connect_t* request, int status);
	static void OnConnection(uv_stream_t* server, int status);
	static void OnAllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buffer);
	static void OnRead(uv_stream_t* stream, ssize_t bytesRead, const uv_buf_t* buffer);
	static void OnWrite(uv_write_t* request, int status);
	static void OnClose(uv_handle_t* handle);
	static void OnShutdown(uv_shutdown_t* request, int status);
	static void OnConnectTimeout(uv_timer_t* timer);

	void StartReceiving();
	void CancelConnectTimeout();

	uv_tcp_t* m_socket = nullptr;
	uv_tcp_t* m_acceptor = nullptr;
	uv_timer_t* m_connectTimer = nullptr;
	sockaddr_storage m_localAddr{};
	bool m_localAddrSet = false;
	RemoteEndpoint m_remoteEndpoint;

	mutable std::mutex m_socketMutex;
	mutable std::mutex m_acceptorMutex;
	mutable std::mutex m_endpointMutex;

	static constexpr size_t kRecvBufferSize = 16384;
};