#pragma once

#include "common/utils/cpu_relax.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

template <typename T, std::size_t Capacity> class RingBuffer {
  static_assert(std::is_trivially_copyable_v<T>,
                "Type must be trivially copyable");
  static_assert(std::has_single_bit(Capacity), "Capacity must be a power of 2");

public:
  void push(const T &item) {
    if (writer_lock_.test_and_set(std::memory_order_acquire)) {
      throw std::logic_error("Violation of single-writer");
    }

    std::size_t seq{seq_.load(std::memory_order_relaxed)};
    seq_.store(seq + 1, std::memory_order_relaxed);

    std::atomic_thread_fence(std::memory_order_release);

    data_[head_ & mask_] = item;
    if ((head_ - tail_) == Capacity) {
      ++tail_;
    }
    ++head_;

    seq_.store(seq + 2, std::memory_order_release);
    writer_lock_.clear(std::memory_order_release);
  }

  [[nodiscard]] std::optional<T> get(std::size_t index) const {
    T item;
    std::size_t seq0{};
    uint64_t spin_count{0};

    do {
      seq0 = seq_.load(std::memory_order_acquire);

      if (seq0 & 1) {
        if (spin_count < 10) {
          cpuRelax();
        } else {
          std::this_thread::yield();
        }
        continue;
      }

      spin_count = 0;

      std::size_t current_head{head_.load(std::memory_order_relaxed)};
      std::size_t current_tail{tail_.load(std::memory_order_relaxed)};

      if (index >= (current_head - current_tail)) {
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq0 != seq_.load(std::memory_order_relaxed)) {
          continue;
        }
        return std::nullopt;
      }

      item = data_[(current_tail + index) & mask_];

      std::atomic_thread_fence(std::memory_order_acquire);

    } while (seq0 != seq_.load(std::memory_order_relaxed));

    return item;
  }

  void copy_to(std::vector<T> &out_vec) const {
    out_vec.resize(Capacity);
    std::size_t seq0{};
    std::size_t current_size{0};
    uint64_t spin_count{0};

    do {
      seq0 = seq_.load(std::memory_order_acquire);
      if (seq0 & 1) {
        if (spin_count < 10) {
          cpuRelax();
        } else {
          std::this_thread::yield();
        }
        continue;
      }

      spin_count = 0;

      std::size_t current_head{head_.load(std::memory_order_relaxed)};
      std::size_t current_tail{tail_.load(std::memory_order_relaxed)};
      current_size = current_head - current_tail;

      for (std::size_t i{0}; i < current_size; ++i) {
        out_vec[i] = data_[(current_tail + i) & mask_];
      }

      std::atomic_thread_fence(std::memory_order_acquire);
    } while (seq0 != seq_.load(std::memory_order_relaxed));

    out_vec.resize(current_size);
  }

private:
  static constexpr std::size_t mask_{Capacity - 1};

  alignas(64) std::atomic<std::size_t> seq_{0};

  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};

  alignas(64) std::array<T, Capacity> data_{};

  std::atomic_flag writer_lock_ = ATOMIC_FLAG_INIT;
};
