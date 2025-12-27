#pragma once

#ifndef _WIN32

#include "socket/SocketBase.h"
#include <uv.h>
#include <atomic>
#include <string>

/**
 * Unix domain socket implementation using libuv pipes.
 *
 * Thread safety:
 * - m_pipe: atomic pointer for lock-free access
 * - m_acceptor: atomic pointer for server socket
 * - All other state follows SocketBase thread safety model
 *
 * Note: Unix sockets are not available on Windows.
 */
class UnixSocket : public SocketBase {
public:
	UnixSocket();
	~UnixSocket() override;

	[[nodiscard]] bool IsOpen() const override;
	bool Bind(const char* path, uint16_t port, bool async = true) override;
	bool Connect(const char* path, uint16_t port, bool async = true) override;
	bool Disconnect() override;
	bool CloseReset() override;
	bool Listen() override;
	bool Send(std::string_view data, bool async = true) override;
	bool SendTo(std::string_view data, const char* hostname, uint16_t port, bool async = true) override;
	bool SetOption(SocketOption option, int value) override;

	[[nodiscard]] std::string GetPath() const { return m_path; }

	/**
	 * Create a UnixSocket from an accepted client handle.
	 * Called from UV thread during OnConnection.
	 */
	static UnixSocket* CreateFromAccepted(uv_pipe_t* clientHandle, const std::string& path);

private:
	void InitPipe();

	void StartReading();

	static void OnAllocBuffer(uv_handle_t* handle, size_t suggestedSize, uv_buf_t* buffer);
	static void OnRead(uv_stream_t* stream, ssize_t bytesRead, const uv_buf_t* buffer);
	static void OnWrite(uv_write_t* request, int status);
	static void OnConnect(uv_connect_t* request, int status);
	static void OnConnection(uv_stream_t* server, int status);
	static void OnClose(uv_handle_t* handle);

	// Atomic pipe pointers for lock-free access
	std::atomic<uv_pipe_t*> m_pipe{nullptr};
	std::atomic<uv_pipe_t*> m_acceptor{nullptr};

	std::string m_path;

	// Receive buffer (Unix sockets are stream-based like TCP)
	static constexpr size_t kRecvBufferSize = 16384;
	char m_recvBuffer[kRecvBufferSize];
};

#endif // _WIN32