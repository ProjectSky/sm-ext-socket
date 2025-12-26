#pragma once

#include "lockfree/SPSCQueue.h"
#include "lockfree/QueueTypes.h"
#include <uv.h>
#include <thread>
#include <atomic>
#include <functional>

/**
 * Lock-free event loop wrapper for libuv.
 *
 * Thread model:
 * - Game thread: posts jobs via Post() method
 * - UV thread: consumes jobs and runs libuv event loop
 *
 * Uses SPSC queue for lock-free job posting.
 */
class EventLoop {
public:
	EventLoop();
	~EventLoop();

	EventLoop(const EventLoop&) = delete;
	EventLoop& operator=(const EventLoop&) = delete;

	void Start();
	void Stop();
	[[nodiscard]] bool IsRunning() const { return m_running.load(std::memory_order_acquire); }
	[[nodiscard]] uv_loop_t* GetLoop() { return m_loop; }

	/**
	 * Post a job to be executed on the UV thread.
	 * Thread-safe, can be called from any thread.
	 *
	 * @param callback Function pointer to execute
	 * @param data User data to pass to callback
	 * @return true if job was queued, false if queue is full
	 */
	bool Post(void (*callback)(void*), void* data);

	/**
	 * Post a job using std::function (convenience wrapper).
	 * Note: This allocates memory for the function object.
	 *
	 * @param callback Function to execute
	 * @return true if job was queued, false if queue is full
	 */
	bool Post(std::function<void()> callback);

private:
	void Run();
	static void OnAsync(uv_async_t* handle);
	static void OnClose(uv_handle_t* handle);

	uv_loop_t* m_loop = nullptr;
	uv_async_t* m_async = nullptr;
	std::thread m_thread;
	std::atomic<bool> m_running{false};
	std::atomic<bool> m_stopping{false};

	// SPSC queue for async jobs (game thread produces, UV thread consumes)
	SPSCQueue<AsyncJob, 1024> m_jobQueue;
};

extern EventLoop g_EventLoop;