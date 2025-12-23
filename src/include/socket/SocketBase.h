#pragma once

#include "socket/SocketTypes.h"
#include <smsdk_ext.h>
#include <uv.h>
#include <string>
#include <string_view>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <functional>

struct CallbackInfo {
	IPluginFunction* function = nullptr;
	cell_t data = 0;
};

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

	int GetOption(SocketOption option) const {
		std::scoped_lock lock(m_optionsMutex);
		auto it = m_options.find(option);
		return it != m_options.end() ? it->second : 0;
	}

	int32_t m_smHandle = 0;

protected:
	void QueueOption(SocketOption option, int value);

	bool SetSocketOption(uv_os_sock_t socketFd, SocketOption option, int value);

	SocketType m_type;
	CallbackInfo m_callbacks[static_cast<size_t>(CallbackEvent::Count)];
	std::queue<PendingOption> m_pendingOptions;
	std::unordered_map<SocketOption, int> m_options;
	mutable std::mutex m_optionsMutex;

	mutable std::shared_mutex m_handlerMutex;
};