#include "2fish/models/market_snapshot.h"
#include "2fish/models/trade.h"
#include "2fish/renderer/chart_renderer.h"

#include "moodycamel/readerwriterqueue.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <iterator>

#include <vector>

renderer::ChartRenderer::ChartRenderer(moodycamel::ReaderWriterQueue<market::Trade>& trade_queue)
	: heatmap_render_buffer_(kHistorySteps* kPriceLevels, 0.0)
	, trade_queue_{ trade_queue }
{
}

void renderer::ChartRenderer::init() {
	static const ImVec4 colors[] = {
		ImVec4(0.01f, 0.01f, 0.05f, 1.00f), // dark navy
		ImVec4(0.02f, 0.05f, 0.15f, 1.00f),
		ImVec4(0.20f, 0.30f, 0.20f, 1.00f),
		ImVec4(0.50f, 0.65f, 0.15f, 1.00f),
		ImVec4(0.80f, 0.95f, 0.05f, 1.00f),
		ImVec4(1.00f, 1.00f, 0.00f, 1.00f)  // neon yellow
	};
	bookmap_colormap_ = ImPlot::AddColormap("Bookmap", colors, std::size(colors));
	active_candle_.start_time_ = ImGui::GetTime();
}

void renderer::ChartRenderer::updateAndDraw(const MarketSnapshot* snapshot) {
	double current_time{ ImGui::GetTime() };

	updateHeatmap(snapshot, current_time);
	updateCandlesticks(current_time);

	ImGuiViewport* viewport{ ImGui::GetMainViewport() };
	ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.7f, viewport->WorkSize.y), ImGuiCond_Always);

	ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground;

	if (ImGui::Begin("Orderbook heatmap", nullptr, window_flags)) {
		ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0, 0, 0, 1));
		ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0, 0, 0, 1));
		ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0, 0));
		if (bookmap_colormap_ != -1) {
			ImPlot::PushColormap(bookmap_colormap_);
		}

		ImPlotFlags plot_flags = ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText | ImPlotFlags_NoTitle | ImPlotFlags_NoFrame;

		if (ImPlot::BeginPlot("Orderbook", ImVec2(-1, -1), plot_flags)) {

			int y_min{ kPriceLevels };
			int y_max{ 0 };
			for (std::size_t i{ 0 }; i < candlestick_history_.size(); ++i) {
				double age_sec{ current_time - candlestick_history_[i].start_time_ };
				double x_pos{ kHistorySteps - (age_sec / 0.1) };

				if (x_pos >= 0) {
					y_min = std::min(y_min, candlestick_history_[i].low_);
					y_max = std::max(y_max, candlestick_history_[i].high_);
				}
			}
			y_min = std::min({ y_min, active_candle_.low_, last_trade_price_ });
			y_max = std::max({ y_max, active_candle_.high_, last_trade_price_ });

			if (y_max == 0) {
				y_min = 0;
				y_max = kPriceLevels;
			}
			else {
				y_min = std::max(0, y_min - 10);
				y_max = std::min(static_cast<int>(kPriceLevels), y_max + 10);
			}

			ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoLabel);
			ImPlot::SetupAxisLimits(ImAxis_X1, 0, kHistorySteps, ImPlotCond_Always);

			ImPlot::SetupAxisLimits(ImAxis_Y1, y_min, y_max, ImPlotCond_Always);

			ImPlot::PlotHeatmap("Liquidity", heatmap_render_buffer_.data(), kPriceLevels, kHistorySteps, 0.0, 1.0, nullptr, ImPlotPoint(0, 0), ImPlotPoint(kHistorySteps, kPriceLevels));

			ImDrawList* draw_list{ ImPlot::GetPlotDrawList() };

			ImPlot::PushPlotClipRect();

			float trade_y_pixels = ImPlot::PlotToPixels(0.0, static_cast<double>(last_trade_price_)).y;
			ImVec2 plot_pos = ImPlot::GetPlotPos();
			ImVec2 plot_size = ImPlot::GetPlotSize();

			/*
			TODO: move these constants away, for now they are:
			dash_length = 5.0f
			gap_length = 4.0f
			line_thickness = 3.0f
			dash_color = IM_COL32(255, 165, 0, 200)
			*/
			// draw a dashed horizontal line representing last trade price
			for (float x{ plot_pos.x }; x < plot_pos.x + plot_size.x; x += 5.0f + 4.0f) {
				ImVec2 start(x, trade_y_pixels);
				ImVec2 end(std::min(x + 5.0f, plot_pos.x + plot_size.x), trade_y_pixels);
				draw_list->AddLine(start, end, IM_COL32(255, 165, 0, 200), 3.0f);
			}

			// draw candlesticks
			for (std::size_t i{ 0 }; i < candlestick_history_.size(); ++i) {
				drawCandlestick(candlestick_history_[i], draw_list, current_time);
			}
			drawCandlestick(active_candle_, draw_list, current_time);

			ImPlot::PopPlotClipRect();

			ImPlot::EndPlot();
		}

		if (bookmap_colormap_ != -1) ImPlot::PopColormap();
		ImPlot::PopStyleVar();
		ImPlot::PopStyleColor(2);
	}
	ImGui::End();
}

void renderer::ChartRenderer::updateHeatmap(const MarketSnapshot* snapshot, double current_time) {
	if (snapshot) {
		// TODO: do not hard code time interval
		if (current_time - last_shift_time_ >= 0.1) {
			for (size_t row = 0; row < kPriceLevels; ++row) {
				double* row_start{ &heatmap_render_buffer_[row * kHistorySteps] };
				std::memmove(row_start, row_start + 1, (kHistorySteps - 1) * sizeof(double));
			}
			heatmap_history_.push(*snapshot);
			last_shift_time_ = current_time;
		}

		const std::size_t last_col{ kHistorySteps - 1 };
		double current_log_max{ 0.0 };

		for (std::size_t level{ 0 }; level < kPriceLevels; ++level) {
			double total_volume{ snapshot->bids_[level] + snapshot->asks_[level] };
			std::size_t render_row{ (kPriceLevels - 1) - level };

			double log_val{ std::log1p(total_volume) };
			heatmap_render_buffer_[render_row * kHistorySteps + last_col] = log_val;
			current_log_max = std::max(current_log_max, log_val);
		}

		max_volume_ = std::max(1.0, std::max(max_volume_ * 0.95, current_log_max));

		for (std::size_t level{ 0 }; level < kPriceLevels; ++level) {
			std::size_t render_row{ (kPriceLevels - 1) - level };
			double& cell{ heatmap_render_buffer_[render_row * kHistorySteps + last_col] };

			cell = std::pow(cell / max_volume_, 2.5); // TODO: slider for power curve exponent
		}
	}
}

void renderer::ChartRenderer::updateCandlesticks(double current_time) {
	// TODO: 1.0 is the time interval in which a candle is pushed to history, make this a global constant
	if (current_time - active_candle_.start_time_ >= 1.0) {
		candlestick_history_.push(active_candle_);

		int last_price{ active_candle_.close_ };
		active_candle_.start_time_ = current_time;
		active_candle_.open_ = last_price;
		active_candle_.high_ = last_price;
		active_candle_.low_ = last_price;
		active_candle_.close_ = last_price;
		active_candle_.volume_ = 0.0;
	}
	while (trade_queue_.try_dequeue(trade_accumulator_)) {
		// build the candle
		active_candle_.high_ = std::max(active_candle_.high_, trade_accumulator_.price_);
		active_candle_.low_ = std::min(active_candle_.low_, trade_accumulator_.price_);
		active_candle_.close_ = trade_accumulator_.price_;
		active_candle_.volume_ += trade_accumulator_.size_;
		last_trade_price_ = trade_accumulator_.price_;
	}
}

void renderer::ChartRenderer::drawCandlestick(const Candlestick& candle, ImDrawList* draw_list, double current_time) {
	double age_sec{ current_time - candle.start_time_ };
	double x_pos{ kHistorySteps - (age_sec / 0.1) };

	if (x_pos < 0) {
		return;
	}

	ImU32 color{ (candle.close_ >= candle.open_) ? IM_COL32(5, 101, 23, 255) : IM_COL32(222, 26, 36, 255) };

	float half_width{ 4.5f };
	ImVec2 p_open{ ImPlot::PlotToPixels(x_pos - half_width, candle.open_) };
	ImVec2 p_close{ ImPlot::PlotToPixels(x_pos + half_width, candle.close_) };
	ImVec2 p_high{ ImPlot::PlotToPixels(x_pos, candle.high_) };
	ImVec2 p_low{ ImPlot::PlotToPixels(x_pos, candle.low_) };

	draw_list->AddLine(p_low, p_high, color, 7.5f);

	float top_y{ std::min(p_open.y, p_close.y) };
	float bottom_y{ std::max(p_open.y, p_close.y) };

	// Even if there is no change, always draw a thin line
	if (bottom_y - top_y < 5.0f) {
		bottom_y = top_y + 5.0f;
	}

	draw_list->AddRectFilled(
		ImVec2(std::min(p_open.x, p_close.x), top_y),
		ImVec2(std::max(p_open.x, p_close.x), bottom_y),
		color
	);
}
