#pragma once

#include "socket/SocketTypes.h"
#include <smsdk_ext.h>
#include <uv.h>
#include <string_view>
#include <queue>
#include <atomic>

struct CallbackInfo {
	IPluginFunction* function = nullptr;
	cell_t data = 0;
};

/**
 * Base class for all socket types.
 *
 * Thread safety:
 * - m_deleted: atomic flag checked by UV thread before enqueuing callbacks
 * - m_options: atomic array for thread-safe option access
 * - m_callbacks: only accessed from game thread
 * - m_pendingOptions: only accessed from game thread (queued) and UV thread (applied)
 */
class SocketBase {
public:
	explicit SocketBase(SocketType type);
	virtual ~SocketBase();

	SocketBase(const SocketBase&) = delete;
	SocketBase& operator=(const SocketBase&) = delete;

	[[nodiscard]] virtual bool IsOpen() const = 0;
	virtual bool Bind(const char* hostname, uint16_t port, bool async = true) = 0;
	virtual bool Connect(const char* hostname, uint16_t port, bool async = true) = 0;
	virtual bool Disconnect() = 0;
	virtual bool CloseReset() { return false; }  // TCP only
	virtual bool Listen() = 0;
	virtual bool Send(std::string_view data, bool async = true) = 0;
	virtual bool SendTo(std::string_view data, const char* hostname, uint16_t port, bool async = true) = 0;
	virtual bool SetOption(SocketOption option, int value) = 0;

	[[nodiscard]] SocketType GetType() const { return m_type; }

	CallbackInfo& GetCallback(CallbackEvent event) {
		return m_callbacks[static_cast<size_t>(event)];
	}

	/**
	 * Get socket option value.
	 * Thread-safe via atomic access.
	 */
	[[nodiscard]] int GetOption(SocketOption option) const {
		size_t index = static_cast<size_t>(option);
		if (index < kMaxOptions) {
			return m_options[index].load(std::memory_order_acquire);
		}
		return 0;
	}

	/**
	 * Check if socket is marked for deletion.
	 * Thread-safe, used by UV thread to skip callbacks.
	 */
	[[nodiscard]] bool IsDeleted() const {
		return m_deleted.load(std::memory_order_acquire);
	}

	/**
	 * Mark socket as deleted.
	 * Called from destructor.
	 */
	void MarkDeleted() {
		m_deleted.store(true, std::memory_order_release);
	}

	int32_t m_smHandle = 0;

protected:
	/**
	 * Queue an option to be applied when socket is initialized.
	 * Called from game thread.
	 */
	void QueueOption(SocketOption option, int value);

	/**
	 * Store option value atomically.
	 * Called from game thread.
	 */
	void StoreOption(SocketOption option, int value) {
		size_t index = static_cast<size_t>(option);
		if (index < kMaxOptions) {
			m_options[index].store(value, std::memory_order_release);
		}
	}

	bool SetSocketOption(uv_os_sock_t socketFd, SocketOption option, int value);

	/**
	 * Apply all pending options to the socket handle.
	 * Called from UV thread after socket initialization.
	 */
	void ApplyPendingOptions(uv_handle_t* handle);

	SocketType m_type;
	CallbackInfo m_callbacks[static_cast<size_t>(CallbackEvent::Count)];

	// Pending options queue (game thread writes, UV thread reads during apply)
	std::queue<PendingOption> m_pendingOptions;

	// Atomic deletion flag
	std::atomic<bool> m_deleted{false};

private:
	static constexpr size_t kMaxOptions = 32;
	std::atomic<int> m_options[kMaxOptions]{};
};