#include "core/EventLoop.h"

EventLoop g_EventLoop;

// Helper struct for std::function wrapper
struct FunctionWrapper {
	std::function<void()> func;
};

static void ExecuteFunctionWrapper(void* data) {
	auto* wrapper = static_cast<FunctionWrapper*>(data);
	if (wrapper) {
		wrapper->func();
		delete wrapper;
	}
}

EventLoop::EventLoop() {
	m_loop = new uv_loop_t;
	uv_loop_init(m_loop);

	m_async = new uv_async_t;
	uv_async_init(m_loop, m_async, OnAsync);
	m_async->data = this;
}

EventLoop::~EventLoop() {
	if (m_running.load(std::memory_order_acquire)) {
		Stop();
	}

	if (m_async) {
		uv_close(reinterpret_cast<uv_handle_t*>(m_async), OnClose);
		uv_run(m_loop, UV_RUN_NOWAIT);
	}

	if (m_loop) {
		uv_loop_close(m_loop);
		delete m_loop;
		m_loop = nullptr;
	}
}

void EventLoop::Start() {
	if (m_running.load(std::memory_order_acquire)) {
		return;
	}

	m_running.store(true, std::memory_order_release);
	m_stopping.store(false, std::memory_order_release);
	m_thread = std::thread(&EventLoop::Run, this);
}

void EventLoop::Stop() {
	if (!m_running.load(std::memory_order_acquire)) {
		return;
	}

	m_stopping.store(true, std::memory_order_release);

	uv_async_send(m_async);

	// Stop the loop
	uv_stop(m_loop);

	if (m_thread.joinable()) {
		m_thread.join();
	}

	m_running.store(false, std::memory_order_release);
}

void EventLoop::Run() {
	while (!m_stopping.load(std::memory_order_acquire)) {
		uv_run(m_loop, UV_RUN_DEFAULT);

		if (!m_stopping.load(std::memory_order_acquire)) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
}

bool EventLoop::Post(void (*callback)(void*), void* data) {
	AsyncJob job(callback, data);

	if (!m_jobQueue.try_enqueue(std::move(job))) {
		return false;
	}

	uv_async_send(m_async);
	return true;
}

bool EventLoop::Post(std::function<void()> callback) {
	// Allocate wrapper on heap (will be freed by ExecuteFunctionWrapper)
	auto* wrapper = new FunctionWrapper{std::move(callback)};

	if (!Post(ExecuteFunctionWrapper, wrapper)) {
		delete wrapper;
		return false;
	}

	return true;
}

void EventLoop::OnAsync(uv_async_t* handle) {
	auto* self = static_cast<EventLoop*>(handle->data);

	// Process all pending jobs
	AsyncJob job;
	while (self->m_jobQueue.try_dequeue(job)) {
		if (job.callback) {
			job.callback(job.data);
		}
	}
}

void EventLoop::OnClose(uv_handle_t* handle) {
	if (handle->type == UV_ASYNC) {
		delete reinterpret_cast<uv_async_t*>(handle);
	}
}