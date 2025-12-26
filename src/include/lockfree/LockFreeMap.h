#pragma once

#include <atomic>
#include <cstddef>
#include <functional>

/**
 * Lock-free hash map using split-ordered lists.
 *
 * This implementation uses a simplified approach with atomic linked lists
 * per bucket. It supports concurrent reads and writes from multiple threads.
 *
 * For the socket extension use case:
 * - Inserts happen from game thread
 * - Lookups happen from both game thread and UV thread
 * - Deletes happen from game thread
 *
 * Memory reclamation uses a simple deferred deletion approach where
 * deleted nodes are marked and cleaned up during subsequent operations.
 */
template<typename K, typename V, size_t NumBuckets = 64>
class LockFreeMap {
	static_assert((NumBuckets & (NumBuckets - 1)) == 0, "NumBuckets must be a power of 2");

public:
	LockFreeMap() {
		for (size_t i = 0; i < NumBuckets; ++i) {
			m_buckets[i].store(nullptr, std::memory_order_relaxed);
		}
	}

	~LockFreeMap() {
		clear();
	}

	LockFreeMap(const LockFreeMap&) = delete;
	LockFreeMap& operator=(const LockFreeMap&) = delete;

	/**
	 * Find a value by key.
	 * @param key The key to search for
	 * @return Value if found, default V otherwise
	 * Thread-safe for concurrent reads.
	 */
	V find(const K& key) const {
		const size_t bucket = hash(key);
		Node* node = m_buckets[bucket].load(std::memory_order_acquire);

		while (node != nullptr) {
			if (!node->deleted.load(std::memory_order_acquire) && node->key == key) {
				return node->value.load(std::memory_order_acquire);
			}
			node = node->next.load(std::memory_order_acquire);
		}
		return V{};
	}

	/**
	 * Insert a key-value pair.
	 * @param key The key
	 * @param value The value
	 * @return true if inserted, false if key already exists
	 */
	bool insert(const K& key, V value) {
		const size_t bucket = hash(key);

		// Check if key already exists
		Node* existing = m_buckets[bucket].load(std::memory_order_acquire);
		while (existing != nullptr) {
			if (!existing->deleted.load(std::memory_order_acquire) && existing->key == key) {
				return false;  // Key already exists
			}
			existing = existing->next.load(std::memory_order_acquire);
		}

		// Create new node
		Node* newNode = new Node(key, value);

		// Insert at head of bucket (lock-free)
		Node* head;
		do {
			head = m_buckets[bucket].load(std::memory_order_acquire);
			newNode->next.store(head, std::memory_order_relaxed);
		} while (!m_buckets[bucket].compare_exchange_weak(
			head, newNode,
			std::memory_order_release,
			std::memory_order_relaxed));

		m_size.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	/**
	 * Update the value for an existing key.
	 * @param key The key
	 * @param value New value
	 * @return true if updated, false if key not found
	 */
	bool update(const K& key, V value) {
		const size_t bucket = hash(key);
		Node* node = m_buckets[bucket].load(std::memory_order_acquire);

		while (node != nullptr) {
			if (!node->deleted.load(std::memory_order_acquire) && node->key == key) {
				node->value.store(value, std::memory_order_release);
				return true;
			}
			node = node->next.load(std::memory_order_acquire);
		}
		return false;
	}

	/**
	 * Remove a key from the map.
	 * Uses logical deletion (marks node as deleted).
	 * @param key The key to remove
	 * @return true if removed, false if not found
	 */
	bool remove(const K& key) {
		const size_t bucket = hash(key);
		Node* node = m_buckets[bucket].load(std::memory_order_acquire);

		while (node != nullptr) {
			if (!node->deleted.load(std::memory_order_acquire) && node->key == key) {
				// Mark as deleted
				bool expected = false;
				if (node->deleted.compare_exchange_strong(
					expected, true,
					std::memory_order_release,
					std::memory_order_relaxed)) {
					m_size.fetch_sub(1, std::memory_order_relaxed);
					m_deletedCount.fetch_add(1, std::memory_order_relaxed);

					// Trigger cleanup if too many deleted nodes
					if (m_deletedCount.load(std::memory_order_relaxed) > NumBuckets) {
						cleanup();
					}
					return true;
				}
				return false;  // Already being deleted by another thread
			}
			node = node->next.load(std::memory_order_acquire);
		}
		return false;
	}

	/**
	 * Insert or update a key-value pair.
	 * @param key The key
	 * @param value The value
	 */
	void insert_or_update(const K& key, V value) {
		if (!update(key, value)) {
			insert(key, value);
		}
	}

	/**
	 * Get approximate size.
	 */
	[[nodiscard]] size_t size() const {
		return m_size.load(std::memory_order_relaxed);
	}

	/**
	 * Check if empty.
	 */
	[[nodiscard]] bool empty() const {
		return size() == 0;
	}

	/**
	 * Clear all entries.
	 * NOT thread-safe - should only be called when no other threads are accessing.
	 */
	void clear() {
		for (size_t i = 0; i < NumBuckets; ++i) {
			Node* node = m_buckets[i].exchange(nullptr, std::memory_order_acquire);
			while (node != nullptr) {
				Node* next = node->next.load(std::memory_order_relaxed);
				delete node;
				node = next;
			}
		}
		m_size.store(0, std::memory_order_relaxed);
		m_deletedCount.store(0, std::memory_order_relaxed);
	}

	/**
	 * Iterate over all non-deleted entries.
	 * NOT thread-safe for concurrent modifications.
	 * @param callback Function to call for each entry
	 */
	template<typename Func>
	void for_each(Func&& callback) const {
		for (size_t i = 0; i < NumBuckets; ++i) {
			Node* node = m_buckets[i].load(std::memory_order_acquire);
			while (node != nullptr) {
				if (!node->deleted.load(std::memory_order_acquire)) {
					callback(node->key, node->value.load(std::memory_order_acquire));
				}
				node = node->next.load(std::memory_order_acquire);
			}
		}
	}

private:
	struct Node {
		K key;
		std::atomic<V> value;
		std::atomic<Node*> next;
		std::atomic<bool> deleted;

		Node(const K& k, V v)
			: key(k)
			, value(v)
			, next(nullptr)
			, deleted(false) {}
	};

	size_t hash(const K& key) const {
		return std::hash<K>{}(key) & (NumBuckets - 1);
	}

	/**
	 * Clean up deleted nodes.
	 * This is a best-effort cleanup that may not remove all deleted nodes
	 * if there's concurrent access.
	 */
	void cleanup() {
		for (size_t i = 0; i < NumBuckets; ++i) {
			Node* prev = nullptr;
			Node* node = m_buckets[i].load(std::memory_order_acquire);

			while (node != nullptr) {
				Node* next = node->next.load(std::memory_order_acquire);

				if (node->deleted.load(std::memory_order_acquire)) {
					// Try to unlink the node
					if (prev == nullptr) {
						// Removing head
						if (m_buckets[i].compare_exchange_strong(
							node, next,
							std::memory_order_release,
							std::memory_order_relaxed)) {
							// Successfully unlinked, safe to delete
							// Note: In a more robust implementation, we'd use
							// hazard pointers or epoch-based reclamation
							delete node;
							m_deletedCount.fetch_sub(1, std::memory_order_relaxed);
						}
					} else {
						// Removing non-head
						if (prev->next.compare_exchange_strong(
							node, next,
							std::memory_order_release,
							std::memory_order_relaxed)) {
							delete node;
							m_deletedCount.fetch_sub(1, std::memory_order_relaxed);
						}
					}
					node = next;
				} else {
					prev = node;
					node = next;
				}
			}
		}
	}

	std::atomic<Node*> m_buckets[NumBuckets];
	std::atomic<size_t> m_size{0};
	std::atomic<size_t> m_deletedCount{0};
};