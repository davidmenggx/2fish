#pragma once

#include "2fish/models/market_accumulation.h"

#include <simdjson.h>

#include <array>
#include <cstdint>
#include <memory>
#include <stack>

constexpr std::size_t PARSER_BUFFER_COUNT{ 2048 }; // TODO: perhaps not constexpr so i can set in ctor

namespace parser {
	class ParserBufferPool {
	public:
		ParserBufferPool();

		market::MarketAccumulation* acquire();

		void release(market::MarketAccumulation* buffer);

	private:
		std::unique_ptr<std::array<market::MarketAccumulation, PARSER_BUFFER_COUNT>> buffers_;

		// TODO: profile average buffer usage
		std::stack<std::size_t> free_buffers_{};
	};
}
