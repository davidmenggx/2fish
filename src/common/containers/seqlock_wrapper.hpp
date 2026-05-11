#pragma once

#include "common/utils/cpu_relax.hpp"

#include <atomic>
#include <thread>
#include <type_traits>

template <typename T> class SeqLockWrapper {
  static_assert(std::is_trivially_copyable_v<T>,
                "Type must be trivially copyable");

public:
  template <typename... Args>
  explicit SeqLockWrapper(Args &&...args)
      : data_(std::forward<Args>(args)...) {}

  template <typename Modifier> void write(Modifier &&modifier) {
    if (writer_lock_.test_and_set(std::memory_order_acquire)) {
      throw std::logic_error("Violation of single-writer");
    }

    std::size_t seq{seq_.load(std::memory_order_relaxed)};

    seq_.store(seq + 1, std::memory_order_release);

    modifier(data_);

    seq_.store(seq + 2, std::memory_order_release);
    writer_lock_.clear(std::memory_order_release);
  }

  template <typename Reader>
  auto read(Reader &&reader) const
      -> decltype(reader(std::declval<const T &>())) {
    using ReturnType = decltype(reader(data_));
    ReturnType snapshot;
    std::size_t seq0{}, seq1{};

    do {
      seq0 = seq_.load(std::memory_order_acquire);

      if (seq0 & 1) {
        cpuRelax();
        continue;
      }

      snapshot = reader(data_);

      std::atomic_thread_fence(std::memory_order_acquire);

      seq1 = seq_.load(std::memory_order_relaxed);

    } while (seq0 != seq1);

    return snapshot;
  }

private:
  alignas(64) std::atomic<size_t> seq_{0};
  T data_;

  std::atomic_flag writer_lock_ = ATOMIC_FLAG_INIT;
};
