#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

template <typename Key, typename Value, std::size_t Capacity,
          std::size_t MaxReaders = 64, std::size_t NumBuckets = Capacity * 2>
class SwmrMap {
  static_assert(std::is_trivially_copyable_v<Key>,
               "Key must be trivially copyable");
  static_assert(std::is_trivially_copyable_v<Value>,
                "Value must be trivially copyable");
  static_assert(MaxReaders <= 64, "Max readers limited to 64");

public:
  SwmrMap() {
    for (auto &bucket : buckets) {
      bucket.store(nullptr, std::memory_order_relaxed);
    }
    for (auto &ptr : clock_array) {
      ptr = nullptr;
    }
  }

  ~SwmrMap() {
    for (std::size_t i{0}; i < current_size; ++i) {
      delete clock_array[i];
    }
    for (auto &retired : retired_list) {
      delete retired.node;
    }
  }

  SwmrMap(const SwmrMap &) = delete;
  SwmrMap &operator=(const SwmrMap &) = delete;

  [[nodiscard]] std::optional<Value> get(const Key &key) {
    std::size_t id{get_thread_id()};

    reader_states[id].active.store(true, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    reader_states[id].epoch.store(global_epoch.load(std::memory_order_relaxed),
                                  std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);

    std::optional<Value> result;
    std::size_t bucket_idx = std::hash<Key>{}(key) % NumBuckets;

    Node *curr{buckets[bucket_idx].load(std::memory_order_acquire)};
    while (curr) {
      if (curr->key == key) {
        curr->referenced.store(true, std::memory_order_relaxed);
        result = curr->value;
        break;
      }
      curr = curr->next.load(std::memory_order_acquire);
    }

    std::atomic_thread_fence(std::memory_order_seq_cst);
    reader_states[id].active.store(false, std::memory_order_release);

    return result;
  }

  void put(const Key &key, const Value &value) {
    if (writer_lock.test_and_set(std::memory_order_acquire)) {
      throw std::logic_error("Violation of single-writer");
    }

    std::size_t bucket_idx{std::hash<Key>{}(key) % NumBuckets};
    Node *prev{nullptr};
    Node *curr{buckets[bucket_idx].load(std::memory_order_relaxed)};

    while (curr) {
      if (curr->key == key) {
        std::size_t idx{curr->clock_idx};
        Node *new_node{
            new Node(key, value, idx)}; // Retain existing clock index
        new_node->next.store(curr->next.load(std::memory_order_relaxed),
                             std::memory_order_relaxed);

        if (prev) {
          prev->next.store(new_node, std::memory_order_release);
        } else {
          buckets[bucket_idx].store(new_node, std::memory_order_release);
        }

        clock_array[idx] = new_node;

        retired_list.push_back(
            {curr, global_epoch.load(std::memory_order_relaxed)});

        reclaim_memory();
        writer_lock.clear(std::memory_order_release);
        return;
      }
      prev = curr;
      curr = curr->next.load(std::memory_order_relaxed);
    }

    if (current_size < Capacity) {
      Node *new_node{new Node(key, value, current_size)};
      new_node->next.store(buckets[bucket_idx].load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
      buckets[bucket_idx].store(new_node, std::memory_order_release);
      clock_array[current_size++] = new_node;
    } else {
      std::size_t sweep_iterations{0};

      // Cap eviction loop to avoid infinite spinning
      while (true) {
        Node *victim{clock_array[clock_hand]};

        if (victim->referenced.load(std::memory_order_relaxed) &&
            sweep_iterations < Capacity) {
          victim->referenced.store(false, std::memory_order_relaxed);
          clock_hand = (clock_hand + 1) % Capacity;
          ++sweep_iterations;
        } else {
          Node *new_node{new Node(key, value, clock_hand)};
          remove_from_hash_table(victim);

          retired_list.push_back(
              {victim, global_epoch.load(std::memory_order_relaxed)});

          new_node->next.store(
              buckets[bucket_idx].load(std::memory_order_relaxed),
              std::memory_order_relaxed);
          buckets[bucket_idx].store(new_node, std::memory_order_release);

          clock_array[clock_hand] = new_node;
          clock_hand = (clock_hand + 1) % Capacity;
          break;
        }
      }
    }

    reclaim_memory();
    writer_lock.clear(std::memory_order_release);
  }

private:
  struct Node {
    Key key;
    Value value;
    std::size_t clock_idx;
    std::atomic<bool> referenced{true};
    std::atomic<Node *> next{nullptr};

    Node(Key k, Value v, std::size_t idx)
        : key(std::move(k)), value(std::move(v)), clock_idx(idx) {}
  };

  // EBR state
  struct ReaderState {
    alignas(64) std::atomic<bool> active{false};
    std::atomic<uint64_t> epoch{0};
  };

  struct RetiredNode {
    Node *node;
    uint64_t epoch;
  };

  alignas(64) std::array<ReaderState, MaxReaders> reader_states;
  alignas(64) std::atomic<uint64_t> global_epoch{1};

  inline static std::atomic<uint64_t> active_thread_slots{0};

  struct ThreadRegistry {
    std::size_t id{};
    ThreadRegistry() {
      uint64_t current{active_thread_slots.load(std::memory_order_relaxed)};
      while (true) {
        std::size_t bit{64};
        for (std::size_t i{0}; i < 64; ++i) {
          if ((current & (1ULL << i)) == 0) {
            bit = i;
            break;
          }
        }
        if (bit >= MaxReaders)
          throw std::runtime_error("Exceeded maximum concurrent readers");

        if (active_thread_slots.compare_exchange_weak(
                current, current | (1ULL << bit), std::memory_order_acquire)) {
          id = bit;
          break;
        }
      }
    }
    ~ThreadRegistry() {
      active_thread_slots.fetch_and(~(1ULL << id), std::memory_order_release);
    }
  };

  static std::size_t get_thread_id() {
    thread_local ThreadRegistry registry;
    return registry.id;
  }

  std::vector<RetiredNode> retired_list;

  std::array<std::atomic<Node *>, NumBuckets> buckets;
  std::array<Node *, Capacity> clock_array;
  std::size_t current_size{0};
  std::size_t clock_hand{0};

  std::atomic_flag writer_lock = ATOMIC_FLAG_INIT;

  void remove_from_hash_table(Node *victim) {
    std::size_t bucket_idx{std::hash<Key>{}(victim->key) % NumBuckets};
    Node *prev{nullptr};
    Node *curr{buckets[bucket_idx].load(std::memory_order_relaxed)};

    while (curr) {
      if (curr == victim) {
        if (prev) {
          prev->next.store(curr->next.load(std::memory_order_relaxed),
                           std::memory_order_release);
        } else {
          buckets[bucket_idx].store(curr->next.load(std::memory_order_relaxed),
                                    std::memory_order_release);
        }
        break;
      }
      prev = curr;
      curr = curr->next.load(std::memory_order_relaxed);
    }
  }

  void reclaim_memory() {
    global_epoch.fetch_add(1, std::memory_order_release);
    uint64_t current_global{global_epoch.load(std::memory_order_acquire)};
    uint64_t min_active_epoch{current_global};

    for (std::size_t i{0}; i < MaxReaders; ++i) {
      if (reader_states[i].active.load(std::memory_order_acquire)) {
        uint64_t reader_epoch{
            reader_states[i].epoch.load(std::memory_order_acquire)};
        if (reader_epoch < min_active_epoch) {
          min_active_epoch = reader_epoch;
        }
      }
    }

    auto it = retired_list.begin();
    while (it != retired_list.end()) {
      if (it->epoch < min_active_epoch) {
        delete it->node;
        *it = retired_list.back();
        retired_list.pop_back();
      } else {
        ++it;
      }
    }
  }
};
