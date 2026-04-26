#pragma once

#include "2fish/models/market_snapshot.h"
#include "2fish/utils/ring_buffer.h"

#include <imgui.h>
#include <implot.h>

#include <array>
#include <cstdint>
#include <vector>

namespace renderer {
	class ChartRenderer {
	public:
		ChartRenderer();

		void updateAndDraw(const MarketSnapshot* snapshot);

		void init();

	private:
		uint16_t window_width_{};
		uint16_t window_height_{};

		// how many market snapshots are saved
		static constexpr size_t kHistorySteps{ 128U };
		static constexpr size_t kPriceLevels = 101U;

		RingBuffer<MarketSnapshot, kHistorySteps> heatmap_history_{};

		std::vector<double> heatmap_render_buffer_{};
		double max_volume_{ 1.0 };
		ImPlotColormap bookmap_colormap_{ -1 };

		double last_shift_time_{ 0.0 };
	};
}
