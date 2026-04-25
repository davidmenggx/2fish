#pragma once

#include <array>
#include <bit>
#include <cassert>
#include <stdexcept>
#include <vector>

template <typename T, std::size_t Size>
class RingBuffer {
    static_assert(std::has_single_bit(Size), "Size must be a power of 2");

public:
    void push(const T& item) {
        data_[head_ & mask_] = item;
        if (full()) {
            ++tail_;
        }
        ++head_;
    }

    void pop(T& item) {
        if (empty()) {
            return;
        }
        item = data_[tail_ & mask_];
        ++tail_;
    }

    [[nodiscard]] bool empty() const { 
        return head_ == tail_; 
    }

    [[nodiscard]] bool full() const { 
        return (head_ - tail_) == Size; 
    }

    [[nodiscard]] std::size_t size() const { 
        return head_ - tail_; 
    }

    [[nodiscard]] T& operator[](std::size_t index) {
        if (index >= size()) {
            throw std::out_of_range("Index out of bounds");
        }
        return data_[(tail_ + index) & mask_];
    }

    [[nodiscard]] const T& operator[](std::size_t index) const {
        if (index >= size()) {
            throw std::out_of_range("Index out of bounds");
        }
        return data_[(tail_ + index) & mask_];
    }

private:
    static constexpr std::size_t mask_{ Size - 1 };

    std::array<T, Size> data_{};

    std::size_t head_{ 0 };
    std::size_t tail_{ 0 };
};
