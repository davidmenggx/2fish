#include "2fish/network/network_buffer_pool.h"

#include <iostream>

market::NetworkBufferPool::NetworkBufferPool() {
	for (std::size_t i{ 0 }; i < kBufferCount; ++i) {
		free_buffers_.push(i);
	}
}

market::MessageBuffer* market::NetworkBufferPool::acquire() {
	if (free_buffers_.empty()) {
		// TODO: find out a better long-term error handling method
		std::cerr << "CRITICAL: out of network buffers!";
		return nullptr;
	}

	std::size_t free_idx{ free_buffers_.top() };
	free_buffers_.pop();

	return &buffers_[free_idx];
}

void market::NetworkBufferPool::release(MessageBuffer* buffer) {
	// a neat trick to find the index with ptr arithmatic
	auto idx{ buffer - &buffers_[0] };

	free_buffers_.push(static_cast<std::size_t>(idx));
}
