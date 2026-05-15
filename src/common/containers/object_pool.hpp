#pragma once

#include "constants.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <optional>

template <typename T, std::size_t Capacity> class ObjectPool {
  static_assert(std::has_single_bit(Capacity), "Capacity must be a power of 2");
  static_assert(Capacity >= 2, "Capacity must be at least 2");

public:
  template <std::invocable Factory> explicit ObjectPool(Factory factory) {
    for (std::size_t i{0}; i < Capacity; ++i) {
      buffer_[i].sequence.store(i + 1, std::memory_order_relaxed);
      buffer_[i].data.emplace(factory());
    }

    enqueue_pos_.store(Capacity, std::memory_order_relaxed);
    dequeue_pos_.store(0, std::memory_order_relaxed);
  }

  ObjectPool(const ObjectPool &) = delete;
  ObjectPool &operator=(const ObjectPool &) = delete;

  std::optional<T> get() {
    Cell *cell{nullptr};
    std::size_t pos{dequeue_pos_.load(std::memory_order_relaxed)};

    while (true) {
      cell = &buffer_[pos & (Capacity - 1)];
      std::size_t seq{cell->sequence.load(std::memory_order_acquire)};

      std::intptr_t dif{static_cast<std::intptr_t>(seq) -
                        static_cast<std::intptr_t>(pos + 1)};

      if (dif == 0) {
        if (dequeue_pos_.compare_exchange_weak(pos, pos + 1,
                                               std::memory_order_relaxed)) {
          break;
        }
      } else if (dif < 0) {
        // Sequence is behind, the pool is empty.
        return std::nullopt;
      } else {
        // Another thread claimed this slot, reload pos and try again.
        pos = dequeue_pos_.load(std::memory_order_relaxed);
      }
    }

    std::optional<T> result{std::move(cell->data)};

    cell->sequence.store(pos + Capacity, std::memory_order_release);

    return result;
  }

  bool replace(T obj) {
    Cell *cell{nullptr};
    std::size_t pos{enqueue_pos_.load(std::memory_order_relaxed)};

    while (true) {
      cell = &buffer_[pos & (Capacity - 1)];
      std::size_t seq{cell->sequence.load(std::memory_order_acquire)};

      std::intptr_t dif{static_cast<std::intptr_t>(seq) -
                        static_cast<std::intptr_t>(pos)};

      if (dif == 0) {
        if (enqueue_pos_.compare_exchange_weak(pos, pos + 1,
                                               std::memory_order_relaxed)) {
          break;
        }
      } else if (dif < 0) {
        // Sequence is behind, the pool is full
        return false;
      } else {
        // Another thread claimed this slot, reload pos and try again
        pos = enqueue_pos_.load(std::memory_order_relaxed);
      }
    }

    cell->data = std::move(obj);
    cell->sequence.store(pos + 1, std::memory_order_release);

    return true;
  }

private:
  struct alignas(constants::CACHE_LINE_SIZE) Cell {
    std::atomic<std::size_t> sequence;
    std::optional<T> data;
  };

  alignas(constants::CACHE_LINE_SIZE) std::array<Cell, Capacity> buffer_;
  alignas(constants::CACHE_LINE_SIZE) std::atomic<std::size_t> enqueue_pos_;
  alignas(constants::CACHE_LINE_SIZE) std::atomic<std::size_t> dequeue_pos_;
};
