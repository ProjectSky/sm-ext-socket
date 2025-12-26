#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

#ifdef __cpp_lib_hardware_interference_size
constexpr size_t kCacheLineSize = std::hardware_destructive_interference_size;
#else
constexpr size_t kCacheLineSize = 64;
#endif

/**
 * Lock-free Single-Producer Single-Consumer (SPSC) queue.
 *
 * Based on a bounded ring buffer with separate cache-line aligned
 * head and tail indices to prevent false sharing.
 *
 * Thread safety:
 * - Only ONE thread may call try_enqueue() (producer)
 * - Only ONE thread may call try_dequeue() (consumer)
 * - Producer and consumer may be different threads
 *
 * Memory ordering:
 * - Uses acquire-release semantics for synchronization
 * - Producer: relaxed load of tail, acquire load of head, release store of tail
 * - Consumer: relaxed load of head, acquire load of tail, release store of head
 */
template<typename T, size_t Capacity = 1024>
class SPSCQueue {
	static_assert(Capacity > 0, "Capacity must be greater than 0");
	static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2 for efficient modulo");

public:
	SPSCQueue() = default;
	~SPSCQueue() {
		// Destroy any remaining elements
		T item;
		while (try_dequeue(item)) {}
	}

	SPSCQueue(const SPSCQueue&) = delete;
	SPSCQueue& operator=(const SPSCQueue&) = delete;
	SPSCQueue(SPSCQueue&&) = delete;
	SPSCQueue& operator=(SPSCQueue&&) = delete;

	/**
	 * Try to enqueue an item (producer only).
	 * @param item The item to enqueue (will be moved)
	 * @return true if successful, false if queue is full
	 */
	bool try_enqueue(T&& item) {
		const size_t tail = m_tail.load(std::memory_order_relaxed);
		const size_t next = (tail + 1) & kMask;

		// Check if queue is full
		if (next == m_cachedHead) {
			m_cachedHead = m_head.load(std::memory_order_acquire);
			if (next == m_cachedHead) {
				return false;
			}
		}

		// Store the item
		new (&m_buffer[tail]) T(std::move(item));

		// Publish the item
		m_tail.store(next, std::memory_order_release);
		return true;
	}

	/**
	 * Try to enqueue an item by copy (producer only).
	 * @param item The item to enqueue (will be copied)
	 * @return true if successful, false if queue is full
	 */
	bool try_enqueue(const T& item) {
		const size_t tail = m_tail.load(std::memory_order_relaxed);
		const size_t next = (tail + 1) & kMask;

		if (next == m_cachedHead) {
			m_cachedHead = m_head.load(std::memory_order_acquire);
			if (next == m_cachedHead) {
				return false;
			}
		}

		new (&m_buffer[tail]) T(item);
		m_tail.store(next, std::memory_order_release);
		return true;
	}

	/**
	 * Try to dequeue an item (consumer only).
	 * @param item Output parameter for the dequeued item
	 * @return true if successful, false if queue is empty
	 */
	bool try_dequeue(T& item) {
		const size_t head = m_head.load(std::memory_order_relaxed);

		// Check if queue is empty
		if (head == m_cachedTail) {
			m_cachedTail = m_tail.load(std::memory_order_acquire);
			if (head == m_cachedTail) {
				return false;
			}
		}

		// Load the item
		T* ptr = reinterpret_cast<T*>(&m_buffer[head]);
		item = std::move(*ptr);
		ptr->~T();

		// Release the slot
		m_head.store((head + 1) & kMask, std::memory_order_release);
		return true;
	}

	/**
	 * Check if the queue is empty (approximate, may have false positives).
	 * Safe to call from any thread but result may be stale.
	 */
	[[nodiscard]] bool empty() const {
		return m_head.load(std::memory_order_acquire) ==
		       m_tail.load(std::memory_order_acquire);
	}

	/**
	 * Get approximate size (may be inaccurate due to concurrent access).
	 */
	[[nodiscard]] size_t size_approx() const {
		const size_t head = m_head.load(std::memory_order_acquire);
		const size_t tail = m_tail.load(std::memory_order_acquire);
		return (tail - head) & kMask;
	}

	/**
	 * Get the capacity of the queue.
	 */
	[[nodiscard]] static constexpr size_t capacity() {
		return Capacity;
	}

private:
	static constexpr size_t kMask = Capacity - 1;

	// Producer-owned: tail index and cached head
	alignas(kCacheLineSize) std::atomic<size_t> m_tail{0};
	size_t m_cachedHead{0};

	// Consumer-owned: head index and cached tail
	alignas(kCacheLineSize) std::atomic<size_t> m_head{0};
	size_t m_cachedTail{0};

	// Buffer storage (aligned to cache line)
	alignas(kCacheLineSize) typename std::aligned_storage<sizeof(T), alignof(T)>::type m_buffer[Capacity];
};
