#pragma once

#include "2fish/aggregator/aggregator.h"
#include "2fish/constants.h"
#include "2fish/models/candlestick.h"
#include "2fish/models/orderbook_snapshot.h"
#include "2fish/models/trade.h"

#include "moodycamel/readerwriterqueue.h"

#include <imgui.h>
#include <implot.h>

#include <array>
#include <cstdint>
#include <vector>

namespace renderer {
	class ChartRenderer {
	public:
		ChartRenderer(Aggregator& aggregator);

		void draw();

		void init();

	private:
		// helper utilities for rendering
		void drawCandlestick(const Candlestick& candle, ImDrawList* draw_list);

		Aggregator& aggregator_;
		std::vector<Candlestick> active_candles_;
		std::vector<OrderbookSnapshot> active_snapshots_;

		uint16_t window_width_{};
		uint16_t window_height_{};

		std::vector<double> heatmap_render_buffer_{};
		std::size_t heatmap_cols_{};

		ImPlotColormap orderbook_heatmap_lookup_{ -1 };

		double previous_x_max_{ 0.0 };
		double right_edge_ms_{};
		bool auto_scroll_{ true };
		double cached_max_volume_{ 1.0 };

		int chart_zoom_gap_{ 10 };
	};
}
