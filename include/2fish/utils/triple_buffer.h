#pragma once
#include <array>
#include <atomic>
#include <cstddef>

template <typename T>
class TripleBuffer {
public:
    [[nodiscard]] T* getWriterBuffer() {
        return &buffers_[write_head_];
    }

    void publishWriterBuffer() {
        std::size_t next_free_index = idle_head_.exchange(write_head_, std::memory_order_acq_rel);
        write_head_ = next_free_index;
    }

    [[nodiscard]] const T* getReaderBuffer() {
        read_head_ = idle_head_.exchange(read_head_, std::memory_order_acq_rel);
        return &buffers_[read_head_];
    }

private:
    std::array<T, 3> buffers_{};

    alignas(64) std::size_t write_head_{ 0 };
    alignas(64) std::atomic<std::size_t> idle_head_{ 1 };
    alignas(64) std::size_t read_head_{ 2 };
};
