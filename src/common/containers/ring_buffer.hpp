#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

template <typename T, std::size_t Capacity> class RingBuffer {
  static_assert(std::is_trivially_copyable_v<T>,
                "Type must be trivially copyable");
  static_assert(std::has_single_bit(Capacity), "Size must be a power of 2");

public:
  void push(const T &item) {
    std::size_t seq{seq_.load(std::memory_order_relaxed)};
    seq_.store(seq + 1, std::memory_order_relaxed);

    std::atomic_thread_fence(std::memory_order_release);

    data_[head_ & mask_] = item;
    if ((head_ - tail_) == Capacity) {
      ++tail_;
    }
    ++head_;

    seq_.store(seq + 2, std::memory_order_release);
  }

  void pop(T &item) {
    std::size_t seq{seq_.load(std::memory_order_relaxed)};
    seq_.store(seq + 1, std::memory_order_relaxed);

    std::atomic_thread_fence(std::memory_order_release);

    if (head_ != tail_) {
      item = data_[tail_ & mask_];
      ++tail_;
    }

    seq_.store(seq + 2, std::memory_order_release);
  }

  [[nodiscard]] std::optional<T> get(std::size_t index) const {
    T item;
    std::size_t seq0{};

    do {
      seq0 = seq_.load(std::memory_order_acquire);

      if (seq0 & 1) {
        continue;
      }

      std::size_t current_head = head_;
      std::size_t current_tail = tail_;

      if (index >= (current_head - current_tail)) {
        return std::nullopt;
      }

      item = data_[(current_tail + index) & mask_];

      std::atomic_thread_fence(std::memory_order_acquire);

    } while (seq0 != seq_.load(std::memory_order_relaxed));

    return item;
  }

  void copy_to(std::vector<T> &out_vec) const {
    out_vec.reserve(Capacity);

    std::size_t seq0{};

    do {
      seq0 = seq_.load(std::memory_order_acquire);

      if (seq0 & 1) {
        continue;
      }

      std::size_t current_head = head_;
      std::size_t current_tail = tail_;
      std::size_t current_size = current_head - current_tail;

      out_vec.resize(current_size);

      for (std::size_t i = 0; i < current_size; ++i) {
        out_vec[i] = data_[(current_tail + i) & mask_];
      }

      std::atomic_thread_fence(std::memory_order_acquire);

    } while (seq0 != seq_.load(std::memory_order_relaxed));
  }

  bool update_back(const T &item) {
    std::size_t seq{seq_.load(std::memory_order_relaxed)};
    seq_.store(seq + 1, std::memory_order_relaxed);

    std::atomic_thread_fence(std::memory_order_release);

    bool success{false};

    if (head_ != tail_) {
      data_[(head_ - 1) & mask_] = item;
      success = true;
    }

    seq_.store(seq + 2, std::memory_order_release);

    return success;
  }

  [[nodiscard]] bool empty() const { return size() == 0; }

  [[nodiscard]] bool full() const { return size() == Capacity; }

  [[nodiscard]] std::size_t size() const {
    std::size_t current_head{}, current_tail{};
    std::size_t seq0{};

    do {
      seq0 = seq_.load(std::memory_order_acquire);
      if (seq0 & 1) {
        continue;
      }

      current_head = head_;
      current_tail = tail_;

      std::atomic_thread_fence(std::memory_order_acquire);
    } while (seq0 != seq_.load(std::memory_order_relaxed));

    return current_head - current_tail;
  }

private:
  static constexpr std::size_t mask_{Capacity - 1};

  std::atomic<std::size_t> seq_{0};

  std::array<T, Capacity> data_{};

  std::size_t head_{0};
  std::size_t tail_{0};
};
