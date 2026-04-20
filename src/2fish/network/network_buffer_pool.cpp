#include "2fish/network/network_buffer_pool.h"

#include <array>
#include <iostream>
#include <memory>

market::NetworkBufferPool::NetworkBufferPool() 
	: buffers_{ std::make_unique<std::array<MessageBuffer, kBufferCount>>() }
{
	for (std::size_t i{ 0 }; i < kBufferCount; ++i) {
		free_buffers_.push(i);
	}
}

market::MessageBuffer* market::NetworkBufferPool::acquire() {
	if (free_buffers_.empty()) {
		return nullptr;
	}

	std::size_t free_idx{ free_buffers_.top() };
	free_buffers_.pop();

	return &(*buffers_)[free_idx];
}

void market::NetworkBufferPool::release(MessageBuffer* buffer) {
	// a neat trick to find the index with ptr arithmatic
	auto idx{ buffer - &(*buffers_)[0] };

	free_buffers_.push(static_cast<std::size_t>(idx));
}
