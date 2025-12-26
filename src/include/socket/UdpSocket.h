#pragma once

#include "socket/SocketBase.h"
#include <uv.h>
#include <atomic>

/**
 * UDP socket implementation using libuv.
 *
 * Thread safety:
 * - m_socket: atomic pointer for lock-free access
 * - All other state follows SocketBase thread safety model
 */
class UdpSocket : public SocketBase {
public:
	UdpSocket();
	~UdpSocket() override;

	[[nodiscard]] bool IsOpen() const override;
	bool Bind(const char* hostname, uint16_t port, bool async = true) override;
	bool Connect(const char* hostname, uint16_t port, bool async = true) override;
	bool Disconnect() override;
	bool CloseReset() override;
	bool Listen() override;
	bool Send(std::string_view data, bool async = true) override;
	bool SendTo(std::string_view data, const char* hostname, uint16_t port, bool async = true) override;
	bool SetOption(SocketOption option, int value) override;

	[[nodiscard]] RemoteEndpoint GetLocalEndpoint() const;

private:
	void InitSocket(int addressFamily = AF_INET);

	void StartReceiving();

	static void OnAllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buffer);
	static void OnRecv(uv_udp_t* handle, ssize_t bytesRead, const uv_buf_t* buffer,
					   const struct sockaddr* senderAddress, unsigned flags);
	static void OnSend(uv_udp_send_t* request, int status);
	static void OnClose(uv_handle_t* handle);

	// Atomic socket pointer for lock-free access
	std::atomic<uv_udp_t*> m_socket{nullptr};

	sockaddr_storage m_localAddr{};
	sockaddr_storage m_connectedAddr{};
	bool m_localAddrSet = false;
	std::atomic<bool> m_isConnected{false};

	// Receive buffer (UDP datagrams can be up to 65535 bytes)
	static constexpr size_t kRecvBufferSize = 65536;
	char m_recvBuffer[kRecvBufferSize];
};