#pragma once

#include "2fish/models/market_snapshot.h"
#include "2fish/utils/ring_buffer.h"
#include "2fish/utils/triple_buffer.h"

#include <array>
#include <cstdint>

namespace renderer {
	class ChartRenderer {
	public:
		ChartRenderer() = default;

		void updateAndDraw(const MarketSnapshot* snapshot);

	private:
		uint16_t window_width_{};
		uint16_t window_height_{};

		RingBuffer<MarketSnapshot, 128U> heatmap_history_{};
	};
}
