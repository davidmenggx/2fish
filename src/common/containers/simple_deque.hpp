#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

template <typename T, std::size_t Capacity> class SimpleDeque {
  static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of 2");

  static constexpr std::size_t Mask = Capacity - 1;

public:
  template <bool IsConst> class DequeIterator {
    using DequePtr =
        std::conditional_t<IsConst, const SimpleDeque *, SimpleDeque *>;
    DequePtr deque_;
    std::size_t index_;

  public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = std::conditional_t<IsConst, const T *, T *>;
    using reference = std::conditional_t<IsConst, const T &, T &>;

    DequeIterator(DequePtr d, std::size_t idx) : deque_(d), index_(idx) {}

    reference operator*() const { return deque_->data_[index_ & Mask]; }
    pointer operator->() const { return &deque_->data_[index_ & Mask]; }

    DequeIterator &operator++() {
      ++index_;
      return *this;
    }
    DequeIterator operator++(int) {
      DequeIterator tmp = *this;
      ++index_;
      return tmp;
    }
    DequeIterator &operator--() {
      --index_;
      return *this;
    }
    DequeIterator operator--(int) {
      DequeIterator tmp = *this;
      --index_;
      return tmp;
    }

    DequeIterator &operator+=(difference_type n) {
      index_ += n;
      return *this;
    }
    DequeIterator &operator-=(difference_type n) {
      index_ -= n;
      return *this;
    }

    friend DequeIterator operator+(DequeIterator it, difference_type n) {
      return it += n;
    }
    friend DequeIterator operator-(DequeIterator it, difference_type n) {
      return it -= n;
    }
    friend difference_type operator-(const DequeIterator &a,
                                     const DequeIterator &b) {
      return a.index_ - b.index_;
    }

    bool operator==(const DequeIterator &other) const {
      return index_ == other.index_;
    }
    bool operator!=(const DequeIterator &other) const {
      return index_ != other.index_;
    }
    bool operator<(const DequeIterator &other) const {
      return index_ < other.index_;
    }
    bool operator>(const DequeIterator &other) const {
      return index_ > other.index_;
    }
    bool operator<=(const DequeIterator &other) const {
      return index_ <= other.index_;
    }
    bool operator>=(const DequeIterator &other) const {
      return index_ >= other.index_;
    }
  };

  using iterator = DequeIterator<false>;
  using const_iterator = DequeIterator<true>;

  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  // Iterators
  iterator begin() { return iterator(this, head_); }
  iterator end() { return iterator(this, tail_); }
  const_iterator begin() const { return const_iterator(this, head_); }
  const_iterator end() const { return const_iterator(this, tail_); }

  reverse_iterator rbegin() { return reverse_iterator(end()); }
  reverse_iterator rend() { return reverse_iterator(begin()); }
  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }
  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }

  const_iterator cbegin() const { return const_iterator(this, head_); }
  const_iterator cend() const { return const_iterator(this, tail_); }

  const_reverse_iterator crbegin() const {
    return const_reverse_iterator(end());
  }
  const_reverse_iterator crend() const {
    return const_reverse_iterator(begin());
  }

  bool empty() const { return head_ == tail_; }
  bool full() const { return tail_ - head_ == Capacity; }
  std::size_t size() const { return tail_ - head_; }

  // Modifiers
  void push_back(const T &value) {
    assert(!full() && "Deque is full");
    data_[tail_ & Mask] = value;
    ++tail_;
  }

  void push_back(T &&value) {
    assert(!full() && "Deque is full");
    data_[tail_ & Mask] = std::move(value);
    ++tail_;
  }

  void push_front(const T &value) {
    assert(!full() && "Deque is full");
    --head_; // Relies on unsigned underflow wrapping around
    data_[head_ & Mask] = value;
  }

  void push_front(T &&value) {
    assert(!full() && "Deque is full");
    --head_;
    data_[head_ & Mask] = std::move(value);
  }

  void pop_back() {
    assert(!empty() && "Deque is empty");
    --tail_;
  }

  void pop_front() {
    assert(!empty() && "Deque is empty");
    ++head_;
  }

  T &front() {
    assert(!empty());
    return data_[head_ & Mask];
  }
  const T &front() const {
    assert(!empty());
    return data_[head_ & Mask];
  }

  T &back() {
    assert(!empty());
    return data_[(tail_ - 1) & Mask];
  }
  const T &back() const {
    assert(!empty());
    return data_[(tail_ - 1) & Mask];
  }

private:
  std::array<T, Capacity> data_;
  std::size_t head_{0};
  std::size_t tail_{0};
};
