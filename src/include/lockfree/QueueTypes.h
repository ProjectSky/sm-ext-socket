#pragma once

#include "socket/SocketTypes.h"
#include <cstddef>

class SocketBase;

/**
 * Queue event types for lock-free cross-thread communication.
 *
 * UV thread (producer) -> Game thread (consumer):
 *   - QueuedConnectEvent
 *   - QueuedDataEvent
 *   - QueuedErrorEvent
 *   - QueuedDisconnectEvent
 *   - QueuedListenEvent
 *   - QueuedIncomingEvent
 *
 * Game thread (producer) -> UV thread (consumer):
 *   - AsyncJob
 */

struct QueuedConnectEvent {
	SocketBase* socket;
	RemoteEndpoint remoteEndpoint;
};

struct QueuedDataEvent {
	SocketBase* socket;
	char* data;         // Heap-allocated, consumer must free
	size_t length;
	RemoteEndpoint sender;
};

struct QueuedErrorEvent {
	SocketBase* socket;
	SocketError errorType;
	const char* errorMsg;
};

struct QueuedDisconnectEvent {
	SocketBase* socket;
};

struct QueuedListenEvent {
	SocketBase* socket;
	RemoteEndpoint localEndpoint;
};

struct QueuedIncomingEvent {
	SocketBase* socket;       // Server socket
	SocketBase* newSocket;    // Accepted client socket
	RemoteEndpoint remoteEndpoint;
};

/**
 * Async job for posting work from game thread to UV thread.
 */
struct AsyncJob {
	void (*callback)(void* data);
	void* data;

	AsyncJob() : callback(nullptr), data(nullptr) {}
	AsyncJob(void (*cb)(void*), void* d) : callback(cb), data(d) {}
};