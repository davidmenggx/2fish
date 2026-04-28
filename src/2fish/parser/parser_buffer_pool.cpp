#include "2fish/models/market_accumulation.h"
#include "2fish/parser/parser_buffer_pool.h"

#include <array>
#include <iostream>
#include <memory>

parser::ParserBufferPool::ParserBufferPool()
	: buffers_{ std::make_unique<std::array<market::MarketAccumulation, PARSER_BUFFER_COUNT>>() }
{
	for (auto& buffer : *buffers_) {
		buffer.asset_id_.reserve(100); // TODO: find how long an asset id is
		buffer.price_change_deltas_.reserve(10);
	}
	for (std::size_t i{ 0 }; i < PARSER_BUFFER_COUNT; ++i) {
		free_buffers_.push(i);
	}
}

market::MarketAccumulation* parser::ParserBufferPool::acquire() {
	if (free_buffers_.empty()) {
		return nullptr;
	}

	std::size_t free_idx{ free_buffers_.top() };
	free_buffers_.pop();

	return &(*buffers_)[free_idx];
}

void parser::ParserBufferPool::release(market::MarketAccumulation* buffer) {
	// a neat trick to find the index with ptr arithmatic
	auto idx{ buffer - &(*buffers_)[0] };

	free_buffers_.push(static_cast<std::size_t>(idx));
}
