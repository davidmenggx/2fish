#pragma once

#include "common/utils/cpu_relax.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <thread>
#include <type_traits>

template <typename T, std::size_t Capacity = 64> class LatestSnapshotBuffer {
  static_assert(std::is_trivially_copyable_v<T>,
                "Type must be trivially copyable");
  static_assert(std::has_single_bit(Capacity), "Capacity must be a power of 2");
  static_assert(Capacity > 1, "Must have at least 2 slots");

public:
  LatestSnapshotBuffer() = default;

  void store(const T &new_data) {
    ++writer_idx_;
    std::size_t slot_idx{writer_idx_ & mask_};
    Slot &current_slot{slots_[slot_idx]};

    current_slot.seq.store(current_seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);

    current_slot.data = new_data;

    std::atomic_thread_fence(std::memory_order_release);
    current_slot.seq.store(current_seq + 2, std::memory_order_relaxed);

    latest_published_idx_.store(writer_idx_, std::memory_order_release);
  }

  // Multiple consumer read
  [[nodiscard]] T load() const {
    T copy;
    std::size_t current_writer_idx{}, slot_idx{}, seq0{}, seq1{};

    do {
      current_writer_idx =
          latest_published_idx_.load(std::memory_order_acquire);

      if (current_writer_idx == 0) {
        return T{};
      }

      slot_idx = current_writer_idx & mask_;
      const Slot &target_slot{slots_[slot_idx]};

      seq0 = target_slot.seq.load(std::memory_order_acquire);

      if (seq0 & 1) {
        cpuRelax();
        continue;
      }

      copy = target_slot.data;

      std::atomic_thread_fence(std::memory_order_acquire);

      seq1 = target_slot.seq.load(std::memory_order_relaxed);

    } while (seq0 != seq1);

    return copy;
  }

private:
  struct alignas(64) Slot {
    std::atomic<std::size_t> seq{0};
    T data{};
  };

  std::array<Slot, Capacity> slots_{};

  // Tracks the total number of writes. Used to find the newest slot.
  alignas(64) std::atomic<std::size_t> latest_published_idx_{0};

  // Writer-local counter. No need for atomic because only one thread writes.
  std::size_t writer_idx_{0};

  static constexpr std::size_t mask_{Capacity - 1};
};
