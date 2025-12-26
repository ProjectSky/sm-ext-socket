#pragma once

#include "socket/SocketTypes.h"
#include "lockfree/SPSCQueue.h"
#include "lockfree/QueueTypes.h"
#include <atomic>

class SocketBase;

/**
 * Lock-free callback manager using SPSC queues.
 *
 * Thread model:
 * - UV thread: produces events via EnqueueXxx() methods
 * - Game thread: consumes events via ProcessPendingCallbacks()
 *
 * Each event type has its own queue to avoid the need for variant types
 * and to allow type-safe processing.
 */
class CallbackManager {
public:
	// Enqueue methods (called from UV thread)
	void EnqueueConnect(SocketBase* socket, const RemoteEndpoint& endpoint);
	void EnqueueDisconnect(SocketBase* socket);
	void EnqueueListen(SocketBase* socket, const RemoteEndpoint& localEndpoint);
	void EnqueueIncoming(SocketBase* socket, SocketBase* newSocket, const RemoteEndpoint& remoteEndpoint);
	void EnqueueReceive(SocketBase* socket, const char* data, size_t length);
	void EnqueueReceive(SocketBase* socket, const char* data, size_t length, const RemoteEndpoint& sender);
	void EnqueueError(SocketBase* socket, SocketError errorType, const char* errorMsg);

	// Process callbacks (called from game thread)
	void ProcessPendingCallbacks();

	// Check if there are pending callbacks for a socket
	[[nodiscard]] bool HasPendingCallbacks() const;

private:
	// Execute individual callback types
	void ExecuteConnect(const QueuedConnectEvent& event);
	void ExecuteDisconnect(const QueuedDisconnectEvent& event);
	void ExecuteListen(const QueuedListenEvent& event);
	void ExecuteIncoming(const QueuedIncomingEvent& event);
	void ExecuteReceive(const QueuedDataEvent& event);
	void ExecuteError(const QueuedErrorEvent& event);

	// Helper to check if socket is valid for callback execution
	[[nodiscard]] bool IsSocketValid(SocketBase* socket) const;

	// SPSC queues for each event type
	// UV thread produces, game thread consumes
	SPSCQueue<QueuedConnectEvent, 256> m_connectQueue;
	SPSCQueue<QueuedDisconnectEvent, 256> m_disconnectQueue;
	SPSCQueue<QueuedListenEvent, 64> m_listenQueue;
	SPSCQueue<QueuedIncomingEvent, 256> m_incomingQueue;
	SPSCQueue<QueuedDataEvent, 1024> m_dataQueue;
	SPSCQueue<QueuedErrorEvent, 256> m_errorQueue;
};

extern CallbackManager g_CallbackManager;