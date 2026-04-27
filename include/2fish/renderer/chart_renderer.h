#pragma once

#include "2fish/models/market_snapshot.h"
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
		ChartRenderer(moodycamel::ReaderWriterQueue<market::Trade>& trade_queue);

		void updateAndDraw(const MarketSnapshot* snapshot);

		void init();

	private:
		// OHLC
		struct Candlestick {
			double start_time_{}; // imgui::GetTime()
			int open_{ 50 };
			int high_{ 50 };
			int low_{ 50 };
			int close_{ 50 };
			double volume_{};
		};

		// helper utilities for updating internal state:
		void updateHeatmap(const MarketSnapshot* snapshot, double current_time);
		void updateCandlesticks(double current_time);

		// helper utilities for rendering
		void drawCandlestick(const Candlestick& candle, ImDrawList* draw_list, double current_time);

		uint16_t window_width_{};
		uint16_t window_height_{};

		// how many market snapshots are saved
		static constexpr size_t kHistorySteps{ 256U };
		static constexpr size_t kPriceLevels = 101U;

		RingBuffer<MarketSnapshot, kHistorySteps> heatmap_history_{};
		moodycamel::ReaderWriterQueue<market::Trade>& trade_queue_;

		std::vector<double> heatmap_render_buffer_{};
		double max_volume_{ 1.0 };
		ImPlotColormap bookmap_colormap_{ -1 };

		double last_shift_time_{ 0.0 };

		market::Trade trade_accumulator_;

		Candlestick active_candle_{};
		RingBuffer<Candlestick, kHistorySteps> candlestick_history_{};
	};
}
