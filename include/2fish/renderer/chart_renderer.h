#pragma once

#include "2fish/market_store/market_store.h"
#include "2fish/models/candlestick.h"
#include "2fish/models/orderbook_snapshot.h"
#include "2fish/models/trade.h"
#include "2fish/utils/ring_buffer.h"

#include "moodycamel/readerwriterqueue.h"

#include <imgui.h>
#include <implot.h>

#include <array>
#include <cstdint>
#include <vector>

namespace renderer {
	class ChartRenderer {
	public:
		ChartRenderer() = default;

		void draw(const QueryResult& query);

		void init();

	private:
		// helper utilities for rendering
		void drawCandlestick(const Candlestick& candle, double x_idx, ImDrawList* draw_list,
			ImU32 bull_col, ImU32 bear_col, ImU32 flat_col);

		uint16_t window_width_{};
		uint16_t window_height_{};

		double max_volume_{ 1.0 };
		ImPlotColormap bookmap_colormap_{ -1 };

		int last_trade_price_{ 50 };

		int chart_zoom_gap_{ 10 };
	};
}
