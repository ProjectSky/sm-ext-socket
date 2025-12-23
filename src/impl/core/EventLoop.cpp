#include "core/EventLoop.h"
#include <cassert>

EventLoop g_EventLoop;

EventLoop::EventLoop() {
	m_loop = new uv_loop_t;
	uv_loop_init(m_loop);

	m_async = new uv_async_t;
	uv_async_init(m_loop, m_async, OnAsync);
	m_async->data = this;
}

EventLoop::~EventLoop() {
	if (m_running.load()) {
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
	if (m_running.load()) {
		return;
	}

	m_running.store(true);
	m_stopping.store(false);
	m_thread = std::thread(&EventLoop::Run, this);
}

void EventLoop::Stop() {
	if (!m_running.load()) {
		return;
	}

	m_stopping.store(true);

	uv_async_send(m_async);

	// Stop the loop
	uv_stop(m_loop);

	if (m_thread.joinable()) {
		m_thread.join();
	}

	m_running.store(false);
}

void EventLoop::Run() {
	while (!m_stopping.load()) {
		uv_run(m_loop, UV_RUN_DEFAULT);

		if (!m_stopping.load()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
}

void EventLoop::Post(std::function<void()> callback) {
	{
		std::scoped_lock lock(m_callbackMutex);
		m_pendingCallbacks.push_back(std::move(callback));
	}
	uv_async_send(m_async);
}

void EventLoop::OnAsync(uv_async_t* handle) {
	auto* self = static_cast<EventLoop*>(handle->data);

	std::vector<std::function<void()>> callbacks;
	{
		std::scoped_lock lock(self->m_callbackMutex);
		callbacks.swap(self->m_pendingCallbacks);
	}

	for (auto& cb : callbacks) {
		cb();
	}
}

void EventLoop::OnClose(uv_handle_t* handle) {
	if (handle->type == UV_ASYNC) {
		delete reinterpret_cast<uv_async_t*>(handle);
	}
}