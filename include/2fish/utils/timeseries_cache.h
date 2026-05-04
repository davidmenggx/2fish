#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

template <typename T, std::size_t Capacity, std::size_t Granularity>
class TimeseriesCache {
    static_assert(std::is_trivially_copyable_v<T>, "Type must be trivially copyable");
    static_assert((Capacity& (Capacity - 1)) == 0, "Capacity must be a power of 2");

private:
    // since we will only write to a slot once in its lifetime,
    // we use a seqlock for the slot itself, to avoid contention
    // with the other slots
    struct alignas(64) Slot {
        std::atomic<std::size_t> seq_{ 0 };
        std::atomic<int64_t> timestamp_{ 0 };
        T data_;
    };

    std::array<Slot, Capacity> buffer_{};
    
public:
    void put(int64_t ts, const T& data) {
        std::size_t idx{ (ts / Granularity) & (Capacity - 1) };
        Slot& slot{ buffer_[idx] };

        std::size_t seq{ slot.seq_.load(std::memory_order_relaxed) };

        slot.seq_.store(seq + 1, std::memory_order_release);

        slot.timestamp_.store(ts, std::memory_order_relaxed);
        slot.data_ = data;

        slot.seq_.store(seq + 2, std::memory_order_release);
    }

    std::optional<T> get(int64_t target_ts) const {
        std::size_t idx{ (target_ts / Granularity) & (Capacity - 1) };
        const Slot& slot{ buffer_[idx] };

        std::size_t seq1{}, seq2{};
        int64_t read_ts{};
        T out_data{};

        do {
            seq1 = slot.seq_.load(std::memory_order_acquire);

            if (seq1 & 1) {
                // spin wait
                #if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
                    _mm_pause();
                #elif defined(__aarch64__) || defined(_M_ARM64)
                    __asm__ volatile("yield" ::: "memory");
                #endif
            }

            read_ts = slot.timestamp_.load(std::memory_order_relaxed);
            out_data = slot.data_;

            std::atomic_thread_fence(std::memory_order_acquire);

            seq2 = slot.seq_.load(std::memory_order_acquire);

        } while (seq1 != seq2 || (seq1 & 1));

        if (read_ts == target_ts) {
            return out_data;
        }

        return std::nullopt;
    }
};
