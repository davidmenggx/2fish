#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
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
    for (auto &bucket : buckets_) {
      bucket.store(nullptr, std::memory_order_relaxed);
    }
    for (auto &ptr : clock_array_) {
      ptr = nullptr;
    }
  }

  ~SwmrMap() {
    for (std::size_t i{0}; i < current_size_; ++i) {
      delete clock_array_[i];
    }
    for (auto &retired : retired_list_) {
      delete retired.node_;
    }
  }

  SwmrMap(const SwmrMap &) = delete;
  SwmrMap &operator=(const SwmrMap &) = delete;

  [[nodiscard]] std::optional<Value> get(const Key &key) {
    std::size_t id{get_thread_id()};

    reader_states_[id].active_.store(true, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    reader_states_[id].epoch_.store(global_epoch_.load(std::memory_order_relaxed),
                                  std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);

    std::optional<Value> result;
    std::size_t bucket_idx = std::hash<Key>{}(key) % NumBuckets;

    Node *curr{buckets_[bucket_idx].load(std::memory_order_acquire)};
    while (curr) {
      if (curr->key_ == key) {
        curr->referenced_.store(true, std::memory_order_relaxed);
        result = curr->value_;
        break;
      }
      curr = curr->next_.load(std::memory_order_acquire);
    }

    std::atomic_thread_fence(std::memory_order_seq_cst);
    reader_states_[id].active_.store(false, std::memory_order_release);

    return result;
  }

  void put(const Key &key, const Value &value) {
    if (writer_lock_.test_and_set(std::memory_order_acquire)) {
      throw std::logic_error("Violation of single-writer");
    }

    std::size_t bucket_idx{std::hash<Key>{}(key) % NumBuckets};
    Node *prev{nullptr};
    Node *curr{buckets_[bucket_idx].load(std::memory_order_relaxed)};

    while (curr) {
      if (curr->key_ == key) {
        std::size_t idx{curr->clock_idx_};
        Node *new_node{
            new Node(key, value, idx)}; // Retain existing clock index
        new_node->next_.store(curr->next_.load(std::memory_order_relaxed),
                             std::memory_order_relaxed);

        if (prev) {
          prev->next_.store(new_node, std::memory_order_release);
        } else {
          buckets_[bucket_idx].store(new_node, std::memory_order_release);
        }

        clock_array_[idx] = new_node;

        retired_list_.push_back(
            {curr, global_epoch_.load(std::memory_order_relaxed)});

        reclaim_memory();
        writer_lock_.clear(std::memory_order_release);
        return;
      }
      prev = curr;
      curr = curr->next_.load(std::memory_order_relaxed);
    }

    if (current_size_ < Capacity) {
      Node *new_node{new Node(key, value, current_size_)};
      new_node->next_.store(buckets_[bucket_idx].load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
      buckets_[bucket_idx].store(new_node, std::memory_order_release);
      clock_array_[current_size_++] = new_node;
    } else {
      std::size_t sweep_iterations{0};

      // Cap eviction loop to avoid infinite spinning
      while (true) {
        Node *victim{clock_array_[clock_hand_]};

        if (victim->referenced_.load(std::memory_order_relaxed) &&
            sweep_iterations < Capacity) {
          victim->referenced_.store(false, std::memory_order_relaxed);
          clock_hand_ = (clock_hand_ + 1) % Capacity;
          ++sweep_iterations;
        } else {
          Node *new_node{new Node(key, value, clock_hand_)};
          remove_from_hash_table(victim);

          retired_list_.push_back(
              {victim, global_epoch_.load(std::memory_order_relaxed)});

          new_node->next_.store(
              buckets_[bucket_idx].load(std::memory_order_relaxed),
              std::memory_order_relaxed);
          buckets_[bucket_idx].store(new_node, std::memory_order_release);

          clock_array_[clock_hand_] = new_node;
          clock_hand_ = (clock_hand_ + 1) % Capacity;
          break;
        }
      }
    }

    reclaim_memory();
    writer_lock_.clear(std::memory_order_release);
  }

private:
  struct Node {
    Key key_;
    Value value_;
    std::size_t clock_idx_;
    std::atomic<bool> referenced_{true};
    std::atomic<Node *> next_{nullptr};

    Node(Key k, Value v, std::size_t idx)
        : key_(std::move(k)), value_(std::move(v)), clock_idx_(idx) {}
  };

  // EBR state
  struct ReaderState {
    alignas(64) std::atomic<bool> active_{false};
    std::atomic<uint64_t> epoch_{0};
  };

  struct RetiredNode {
    Node *node_;
    uint64_t epoch_;
  };

  alignas(64) std::array<ReaderState, MaxReaders> reader_states_;
  alignas(64) std::atomic<uint64_t> global_epoch_{1};

  inline static std::atomic<uint64_t> active_thread_slots_{0};

  struct ThreadRegistry {
    std::size_t id_{};
    ThreadRegistry() {
      uint64_t current{active_thread_slots_.load(std::memory_order_relaxed)};
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

        if (active_thread_slots_.compare_exchange_weak(
                current, current | (1ULL << bit), std::memory_order_acquire)) {
          id_ = bit;
          break;
        }
      }
    }
    ~ThreadRegistry() {
      active_thread_slots_.fetch_and(~(1ULL << id_), std::memory_order_release);
    }
  };

  static std::size_t get_thread_id() {
    thread_local ThreadRegistry registry;
    return registry.id_;
  }

  std::vector<RetiredNode> retired_list_;

  std::array<std::atomic<Node *>, NumBuckets> buckets_;
  std::array<Node *, Capacity> clock_array_;
  std::size_t current_size_{0};
  std::size_t clock_hand_{0};

  std::atomic_flag writer_lock_ = ATOMIC_FLAG_INIT;

  void remove_from_hash_table(Node *victim) {
    std::size_t bucket_idx{std::hash<Key>{}(victim->key_) % NumBuckets};
    Node *prev{nullptr};
    Node *curr{buckets_[bucket_idx].load(std::memory_order_relaxed)};

    while (curr) {
      if (curr == victim) {
        if (prev) {
          prev->next_.store(curr->next_.load(std::memory_order_relaxed),
                           std::memory_order_release);
        } else {
          buckets_[bucket_idx].store(curr->next_.load(std::memory_order_relaxed),
                                    std::memory_order_release);
        }
        break;
      }
      prev = curr;
      curr = curr->next_.load(std::memory_order_relaxed);
    }
  }

  void reclaim_memory() {
    global_epoch_.fetch_add(1, std::memory_order_release);
    uint64_t current_global{global_epoch_.load(std::memory_order_acquire)};
    uint64_t min_active_epoch{current_global};

    for (std::size_t i{0}; i < MaxReaders; ++i) {
      if (reader_states_[i].active_.load(std::memory_order_acquire)) {
        uint64_t reader_epoch{
            reader_states_[i].epoch_.load(std::memory_order_acquire)};
        if (reader_epoch < min_active_epoch) {
          min_active_epoch = reader_epoch;
        }
      }
    }

    for (std::size_t i{0}; i < retired_list_.size();) {
      if (retired_list_[i].epoch_ < min_active_epoch) {
        delete retired_list_[i].node_;

        retired_list_[i] = retired_list_.back();
        retired_list_.pop_back();
      } else {
        ++i;
      }
    }
  }
};
