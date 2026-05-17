#pragma once

#include "common/utils/cpu_relax.hpp"
#include "constants.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>

template <typename T, std::size_t Capacity> class RingBuffer {
  static_assert(std::is_trivially_copyable_v<T>,
                "Type must be trivially copyable");
  static_assert(std::has_single_bit(Capacity), "Capacity must be a power of 2");

public:
  void push(const T &item) {
    if (writer_lock_.test_and_set(std::memory_order_acquire))
      throw std::logic_error("Violation of single-writer");

    std::size_t seq{seq_.load(std::memory_order_relaxed)};
    seq_.store(seq + 1, std::memory_order_relaxed);

    std::atomic_thread_fence(std::memory_order_release);

    data_[head_ & mask_] = item;
    if ((head_ - tail_) == Capacity)
      ++tail_;
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
        if (spin_count < 10)
          cpuRelax();
        else
          std::this_thread::yield();
        continue;
      }

      spin_count = 0;

      std::size_t current_head{head_.load(std::memory_order_relaxed)};
      std::size_t current_tail{tail_.load(std::memory_order_relaxed)};

      if (index >= (current_head - current_tail)) {
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq0 != seq_.load(std::memory_order_relaxed))
          continue;
        return std::nullopt;
      }

      item = data_[(current_tail + index) & mask_];

      std::atomic_thread_fence(std::memory_order_acquire);

    } while (seq0 != seq_.load(std::memory_order_relaxed));

    return item;
  }

  template <typename V, typename Compare = std::less<>>
  [[nodiscard]] std::optional<T> prev_upper_bound(const V &value,
                                                  Compare comp = {}) const {
    T item{};
    bool found{false};
    std::size_t seq0{};
    uint64_t spin_count{0};

    do {
      seq0 = seq_.load(std::memory_order_acquire);

      if (seq0 & 1) {
        if (spin_count < 10)
          cpuRelax();
        else
          std::this_thread::yield();
        continue;
      }

      spin_count = 0;

      std::size_t current_head{head_.load(std::memory_order_relaxed)};
      std::size_t current_tail{tail_.load(std::memory_order_relaxed)};
      std::size_t count{current_head - current_tail};

      if (count == 0) {
        std::atomic_thread_fence(std::memory_order_acquire);
        if (seq0 != seq_.load(std::memory_order_relaxed))
          continue;
        return std::nullopt;
      }

      std::size_t first{0};
      std::size_t len{count};

      while (len > 0) {
        std::size_t half{len >> 1};
        std::size_t middle{first + half};
        T mid_val{data_[(current_tail + middle) & mask_]};

        if (comp(value, mid_val)) {
          len = half;
        } else {
          first = middle + 1;
          len = len - half - 1;
        }
      }

      if (first == 0) {
        found = false;
      } else {
        item = data_[(current_tail + first - 1) & mask_];
        found = true;
      }

      std::atomic_thread_fence(std::memory_order_acquire);

    } while (seq0 != seq_.load(std::memory_order_relaxed));

    if (found)
      return item;

    return std::nullopt;
  }

private:
  static constexpr std::size_t mask_{Capacity - 1};

  alignas(constants::CACHE_LINE_SIZE) std::atomic<std::size_t> seq_{0};

  std::atomic<std::size_t> head_{0};
  std::atomic<std::size_t> tail_{0};

  alignas(constants::CACHE_LINE_SIZE) std::array<T, Capacity> data_{};

  std::atomic_flag writer_lock_ = ATOMIC_FLAG_INIT;
};
