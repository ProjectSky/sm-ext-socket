#pragma once

#include <uv.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <memory>

class EventLoop {
public:
	EventLoop();
	~EventLoop();

	EventLoop(const EventLoop&) = delete;
	EventLoop& operator=(const EventLoop&) = delete;

	void Start();
	void Stop();
	[[nodiscard]] bool IsRunning() const { return m_running.load(); }
	[[nodiscard]] uv_loop_t* GetLoop() { return m_loop; }
	void Post(std::function<void()> callback);

private:
	void Run();
	static void OnAsync(uv_async_t* handle);
	static void OnClose(uv_handle_t* handle);

	uv_loop_t* m_loop = nullptr;
	uv_async_t* m_async = nullptr;
	std::thread m_thread;
	std::atomic<bool> m_running{false};
	std::atomic<bool> m_stopping{false};
	std::mutex m_callbackMutex;
	std::vector<std::function<void()>> m_pendingCallbacks;
};

extern EventLoop g_EventLoop;