#pragma once

#include <array>
#include <atomic>

template <typename T>
class TripleBuffer {
public:
	// called by the writer, into which it updates its state
	[[nodiscard]] T* getWriterBuffer() {
		return &buffers_[write_head_];
	}

	void publishWriterBuffer() {
		idle_head_.exchange(write_head_, std::memory_order_acq_rel);
	}

	// called by the reader
	[[nodiscard]] const T* getReaderBuffer() {
		return &buffers_[read_head_];
	}

private:
	std::array<T, 3> buffers_{};

	std::size_t write_head_{ 0 };
	std::atomic<std::size_t> idle_head_{ 1 };
	std::size_t read_head_{ 2 };
};
