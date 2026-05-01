#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

template <typename Key, typename Value, std::size_t Capacity>
class LRUCache {
	static_assert(Capacity > 0, "Capacity must be greater than 0");
	static_assert((Capacity& (Capacity - 1)) == 0, "Capacity must be a power of 2");

	static constexpr std::size_t BucketCount{ Capacity * 2 };
	static constexpr std::size_t BucketMask{ BucketCount - 1 };
	static constexpr uint32_t NULL_INDEX{ 0xFFFFFFFF };

	struct Node {
		Key key;
		Value value;
		uint32_t lru_prev{ NULL_INDEX };
		uint32_t lru_next{ NULL_INDEX };
		uint32_t hash_next{ NULL_INDEX };
	};

public:
	LRUCache() {
		buckets.fill(NULL_INDEX);
	}

	const Value* get(const Key& key) {
		uint32_t idx{ find_in_hash(key) };
		if (idx == NULL_INDEX) {
			return nullptr;
		}

		move_to_front(idx);
		return nodes[idx].value;
	}

	void put(const Key& key, const Value& value) {
		uint32_t h{ hash_func(key) & BucketMask };

		uint32_t curr{ buckets[h] };
		while (curr != NULL_INDEX) {
			if (nodes[curr].key == key) {
				nodes[curr].value = value;
				move_to_front(curr);
				return;
			}
			curr = nodes[curr].hash_next;
		}

		uint32_t new_idx{ NULL_INDEX };

		if (current_size < Capacity) {
			// if there is room for the missed object, just add it
			new_idx = current_size++;
		}
		else {
			// if full capacity, evict the oldest
			new_idx = tail;

			uint32_t evict_h{ hash_func(nodes[new_idx].key) & BucketMask };
			remove_from_hash_chain(new_idx, evict_h);

			remove_from_lru(new_idx);
		}

		nodes[new_idx].key = key;
		nodes[new_idx].value = value;

		nodes[new_idx].hash_next = buckets[h];
		buckets[h] = new_idx;

		add_to_front(new_idx);
	}

private:
	std::array<Node, Capacity> nodes{};
	std::array<uint32_t, BucketCount> buckets{};
	std::hash<Key> hash_func;

	uint32_t head{ NULL_INDEX };
	uint32_t tail{ NULL_INDEX };
	uint32_t current_size{ 0 };

	uint32_t find_in_hash(const Key& key) const {
		uint32_t h{ hash_func(key) & BucketMask };
		uint32_t curr{ buckets[h] };
		while (curr != NULL_INDEX) {
			if (nodes[curr].key == key) {
				return curr;
			}
			curr = nodes[curr].hash_next;
		}
		return NULL_INDEX;
	}

	void remove_from_hash_chain(uint32_t idx, uint32_t h) {
		uint32_t curr{ buckets[h] };
		uint32_t prev{ NULL_INDEX };

		while (curr != NULL_INDEX) {
			if (curr == idx) {
				if (prev == NULL_INDEX) {
					buckets[h] = nodes[curr].hash_next;
				}
				else {
					nodes[prev].hash_next = nodes[curr].hash_next;
				}
				break;
			}
			prev = curr;
			curr = nodes[curr].hash_next;
		}
	}

	void remove_from_lru(uint32_t idx) {
		uint32_t p{ nodes[idx].lru_prev };
		uint32_t n{ nodes[idx].lru_next };

		if (p != NULL_INDEX) {
			nodes[p].lru_next = n;
		}
		else {
			head = n;
		}

		if (n != NULL_INDEX) {
			nodes[n].lru_prev = p;
		}
		else {
			tail = p;
		}
	}

	void add_to_front(uint32_t idx) {
		nodes[idx].lru_prev = NULL_INDEX;
		nodes[idx].lru_next = head;

		if (head != NULL_INDEX) {
			nodes[head].lru_prev = idx;
		}
		head = idx;

		if (tail == NULL_INDEX) {
			tail = idx;
		}
	}

	void move_to_front(uint32_t idx) {
		if (head == idx) {
			return;
		}
		remove_from_lru(idx);
		add_to_front(idx);
	}
};
