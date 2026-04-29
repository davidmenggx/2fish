#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <stdexcept>
#include <vector>

template <typename T, std::size_t Size>
class RingBuffer {
    static_assert(std::has_single_bit(Size), "Size must be a power of 2");

public:
    bool push(const T& item) {
        auto current_head = head_.load(std::memory_order_relaxed);
        auto current_tail = tail_.load(std::memory_order_acquire);

        if ((current_head - current_tail) == Size) {
            return false;
        }

        data_[current_head & mask_] = item;

        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& item) {
        auto current_tail = tail_.load(std::memory_order_relaxed);

        auto current_head = head_.load(std::memory_order_acquire);

        if (current_head == current_tail) {
            return false;
        }

        item = data_[current_tail & mask_];

        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool full() const {
        return (head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire)) == Size;
    }

    [[nodiscard]] std::size_t size() const {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }

    // TODO: returning by reference here is potentially dangerous
    [[nodiscard]] T& operator[](std::size_t index) {
        auto current_tail = tail_.load(std::memory_order_acquire);
        auto current_head = head_.load(std::memory_order_acquire);
        if (index >= (current_head - current_tail)) {
            throw std::out_of_range("Index out of bounds");
        }
        return data_[(current_tail + index) & mask_];
    }

    [[nodiscard]] const T& operator[](std::size_t index) const {
        auto current_tail = tail_.load(std::memory_order_acquire);
        auto current_head = head_.load(std::memory_order_acquire);
        if (index >= (current_head - current_tail)) {
            throw std::out_of_range("Index out of bounds");
        }
        return data_[(current_tail + index) & mask_];
    }

private:
    static constexpr std::size_t mask_{ Size - 1 };

    std::array<T, Size> data_{};

    std::atomic<std::size_t> head_{ 0 };
    std::atomic<std::size_t> tail_{ 0 };
};
