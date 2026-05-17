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

template <typename T, std::size_t Capacity> class Deque {
  static_assert(std::is_trivially_copyable_v<T>,
                "Type must be trivially copyable");
  static_assert(std::has_single_bit(Capacity), "Capacity must be a power of 2");
  static_assert(Capacity > 0, "Capacity must be greater than 0");

  struct Slot {
    std::atomic<std::size_t> seq{0};
    T data{};
  };

public:
  void push_back(const T &item) {
    if (writer_lock_.test_and_set(std::memory_order_acquire))
      throw std::logic_error("Violation of single-writer");

    std::size_t h{head_.load(std::memory_order_relaxed)};
    std::size_t t{tail_.load(std::memory_order_relaxed)};

    // Drop the oldest items if capacity is full
    if (h - t >= Capacity) {
      std::size_t desired_tail{h - Capacity + 1};
      while (t < desired_tail) {
        if (tail_.compare_exchange_weak(t, desired_tail,
                                        std::memory_order_relaxed))
          break;
      }
    }

    std::size_t idx{h & mask_};
    std::size_t expected_seq{h << 1};

    buffer_[idx].seq.store(expected_seq | 1, std::memory_order_release);

    buffer_[idx].data = item;

    buffer_[idx].seq.store(expected_seq, std::memory_order_release);

    head_.store(h + 1, std::memory_order_release);

    writer_lock_.clear(std::memory_order_release);
  }

  [[nodiscard]] std::optional<T> pop_front() {
    T item;
    std::size_t t{tail_.load(std::memory_order_relaxed)};

    while (true) {
      std::size_t h{head_.load(std::memory_order_acquire)};

      if (t >= h)
        return std::nullopt;

      if (!tail_.compare_exchange_weak(t, t + 1, std::memory_order_relaxed))
        continue;

      std::size_t idx{t & mask_};
      std::size_t expected_seq{t << 1};

      std::size_t s1{buffer_[idx].seq.load(std::memory_order_acquire)};

      if (s1 != expected_seq)
        continue;

      item = buffer_[idx].data;

      std::atomic_thread_fence(std::memory_order_acquire);

      std::size_t s2{buffer_[idx].seq.load(std::memory_order_relaxed)};

      if (s1 == s2)
        return item;
    }
  }

private:
  static constexpr std::size_t mask_{Capacity - 1};

  alignas(constants::CACHE_LINE_SIZE) std::atomic<std::size_t> head_{0};
  alignas(constants::CACHE_LINE_SIZE) std::atomic<std::size_t> tail_{0};

  alignas(constants::CACHE_LINE_SIZE) std::array<Slot, Capacity> buffer_{};

  std::atomic_flag writer_lock_ = ATOMIC_FLAG_INIT;
};
