#include "2fish/aggregator/aggregator.h"
#include "2fish/constants.h"
#include "2fish/models/candlestick.h"
#include "2fish/models/orderbook_snapshot.h"
#include "2fish/renderer/chart_renderer.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <vector>

renderer::ChartRenderer::ChartRenderer(Aggregator& aggregator)
	: aggregator_{ aggregator }
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
	orderbook_heatmap_lookup_ = ImPlot::AddColormap("Orderbook Heatmap", colors, std::size(colors));
}

// TODO: you might need to be a global util
static int64_t getLocalTimeMs() {
	auto now = std::chrono::system_clock::now();
	return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

void renderer::ChartRenderer::draw() {
	int64_t local_now_ms{ getLocalTimeMs() };
	int64_t receipt_ts{ aggregator_.getLocalReceiptTime() };
	int64_t exchange_ts{ aggregator_.getLatestExchangeTimestamp() };

	int64_t elapsed_silence_ms{ std::max<int64_t>(0, local_now_ms - receipt_ts) };
	double visual_now_ms{ static_cast<double>(exchange_ts + elapsed_silence_ms) };

	double window_duration_ms{ static_cast<double>(constants::HISTORY_STEPS * constants::CANDLESTICK_INTERVAL) };
	double x_min{ visual_now_ms - window_duration_ms };
	double x_max{ visual_now_ms };

	aggregator_.extractCandles(active_candles_);

	aggregator_.extractOrderbook(active_snapshots_);
	heatmap_cols_ = active_snapshots_.size();

	if (heatmap_cols_ > 0) {
		heatmap_render_buffer_.resize(constants::PRICE_LEVELS * heatmap_cols_);

		double max_visible_volume{ 0.0 };
		for (std::size_t col{ 0 }; col < heatmap_cols_; ++col) {
			const OrderbookSnapshot& snapshot{ active_snapshots_[col] };
			for (std::size_t row{ 0 }; row < constants::PRICE_LEVELS; ++row) {
				double vol{ snapshot.getLiquidity(row) };
				if (vol > max_visible_volume) {
					max_visible_volume = vol;
				}
			}
		}

		if (max_visible_volume < 1.0) {
			max_visible_volume = 1.0;
		}

		double log_max_volume{ std::log1p(max_visible_volume) };

		for (std::size_t col{ 0 }; col < heatmap_cols_; ++col) {
			const OrderbookSnapshot& snapshot{ active_snapshots_[col] };

			for (std::size_t row{ 0 }; row < constants::PRICE_LEVELS; ++row) {
				double liquidity_volume{ snapshot.getLiquidity(row) };

				double log_volume{ std::log1p(liquidity_volume) };

				double normalized_liquidity{ log_volume / log_max_volume };

				std::size_t index{ (row * heatmap_cols_) + col };
				heatmap_render_buffer_[index] = std::clamp(normalized_liquidity, 0.0, 1.0);
			}
		}
	}

	int y_min{ constants::PRICE_LEVELS };
	int y_max{ 0 };
	double latest_close_price{ 0.0 };

	for (const Candlestick& candle : active_candles_) {
		if (static_cast<double>(candle.start_timestamp_) >= x_min) {
			y_min = std::min(y_min, static_cast<int>(candle.low_));
			y_max = std::max(y_max, static_cast<int>(candle.high_));
			latest_close_price = candle.close_;
		}
	}

	if (y_max == 0) {
		y_min = 0;
		y_max = constants::PRICE_LEVELS;
	}
	else {
		y_min = std::max(0, y_min - chart_zoom_gap_);
		y_max = std::min(static_cast<int>(constants::PRICE_LEVELS), y_max + chart_zoom_gap_);
	}

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.7f, viewport->WorkSize.y), ImGuiCond_Always);

	ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground;

	if (ImGui::Begin("Orderbook heatmap", nullptr, window_flags)) {
		ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0, 0, 0, 1));
		ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0, 0, 0, 1));
		ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0, 0));

		if (orderbook_heatmap_lookup_ != -1) {
			ImPlot::PushColormap(orderbook_heatmap_lookup_);
		}

		float old_scale{ ImGui::GetFont()->Scale };
		ImGui::GetFont()->Scale *= 1.5f;
		ImGui::PushFont(ImGui::GetFont());

		ImPlotFlags plot_flags = ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText | ImPlotFlags_NoTitle | ImPlotFlags_NoFrame;

		ImVec2 cached_plot_pos;
		ImVec2 cached_plot_size;
		bool plot_was_drawn{ false };

		if (ImPlot::BeginPlot("Orderbook", ImVec2(-1, -1), plot_flags)) {

			ImPlot::SetupAxes(nullptr, nullptr, 0, ImPlotAxisFlags_Opposite);

			// if the user is up to date on the latest action, auto scroll them.
			// otherwise, let them browse freely
			if (previous_x_max_ == 0.0 || auto_scroll_) {
				ImPlot::SetupAxisLimits(ImAxis_X1, x_min, x_max, ImPlotCond_Always);
			}
			else {
				ImPlot::SetupAxisLimits(ImAxis_X1, x_min, x_max, ImPlotCond_Once);
			}

			ImPlot::SetupAxisLimits(ImAxis_Y1, y_min, y_max, ImPlotCond_Always);

			ImPlot::SetupAxisFormat(ImAxis_X1, [](double value, char* buff, int size, void* user_data) {
				std::time_t t{ static_cast<std::time_t>(value / 1000.0) };
				std::tm* tm_info{ std::gmtime(&t) };
				if (tm_info) {
					return (int)std::strftime(buff, size, "%H:%M:%S", tm_info);
				}
				return 0;
				}, nullptr);

			if (heatmap_cols_ > 0) {
				double first_snap_ms{ static_cast<double>(active_snapshots_.front().timestamp_) };
				double last_snap_ms{ static_cast<double>(active_snapshots_.back().timestamp_) + constants::ORDERBOOK_INTERVAL };

				ImPlotPoint bounds_min(first_snap_ms, 0);
				ImPlotPoint bounds_max(last_snap_ms, constants::PRICE_LEVELS);

				ImPlot::PlotHeatmap("Liquidity", heatmap_render_buffer_.data(),
					constants::PRICE_LEVELS, heatmap_cols_,
					0.0, 1.0, nullptr, bounds_min, bounds_max);
			}

			ImDrawList* draw_list = ImPlot::GetPlotDrawList();
			ImPlot::PushPlotClipRect();

			for (const Candlestick& candle : active_candles_) {
				if (static_cast<double>(candle.start_timestamp_ + constants::CANDLESTICK_INTERVAL) > x_min) {
					drawCandlestick(candle, draw_list);
				}
			}

			if (latest_close_price > 0.0) {
				float trade_y_pixels{ ImPlot::PlotToPixels(0.0, latest_close_price).y };
				ImVec2 current_plot_pos = ImPlot::GetPlotPos();
				ImVec2 current_plot_size = ImPlot::GetPlotSize();

				// draw the last trade line
				for (float x{ current_plot_pos.x }; x < current_plot_pos.x + current_plot_size.x; x += 9.0f) {
					ImVec2 start(x, trade_y_pixels);
					ImVec2 end(std::min(x + 5.0f, current_plot_pos.x + current_plot_size.x), trade_y_pixels);
					draw_list->AddLine(start, end, IM_COL32(255, 165, 0, 200), 2.0f);
				}
			}

			cached_plot_pos = ImPlot::GetPlotPos();
			cached_plot_size = ImPlot::GetPlotSize();
			plot_was_drawn = true;

			previous_x_max_ = ImPlot::GetPlotLimits(ImAxis_X1).X.Max;

			if (ImPlot::IsPlotHovered() && (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::GetIO().MouseWheel != 0.0f)) {
				auto_scroll_ = false;
			}
			else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && previous_x_max_ >= x_max - (window_duration_ms * 0.05)) {
				auto_scroll_ = true;
			}

			ImPlot::PopPlotClipRect();
			ImPlot::EndPlot();
		}

		if (plot_was_drawn) {
			ImVec2 plot_max = ImVec2(cached_plot_pos.x + cached_plot_size.x, cached_plot_pos.y + cached_plot_size.y);

			if (ImGui::IsMouseHoveringRect(cached_plot_pos, plot_max)) {
				// TODO: move these constants out
				float button_width{ 80.0f };
				float button_height{ 40.0f };
				float spacing{ ImGui::GetStyle().ItemSpacing.x };
				float total_width{ (button_width * 2) + spacing };

				ImVec2 button_start_pos = ImVec2(
					cached_plot_pos.x + (cached_plot_size.x - total_width) * 0.5f,
					plot_max.y - button_height - 20.0f
				);

				ImGui::SetCursorScreenPos(button_start_pos);

				// TODO: move this constant out
				int zoom_step{ 3 };

				// zoom in
				if (ImGui::Button("+", ImVec2(button_width, button_height))) {
					chart_zoom_gap_ = std::max(zoom_step, chart_zoom_gap_ - zoom_step);
				}

				ImGui::SameLine();

				// zoom out
				if (ImGui::Button("-", ImVec2(button_width, button_height))) {
					chart_zoom_gap_ += zoom_step;
				}

				ImGui::SameLine();

				// back to live feed
				if (ImGui::Button("Live", ImVec2(button_width, button_height))) {
					previous_x_max_ = 0.0;
					auto_scroll_ = true;
				}
			}
		}

		if (orderbook_heatmap_lookup_ != -1) {
			ImPlot::PopColormap();
		}
		ImPlot::PopStyleVar();
		ImPlot::PopStyleColor(2);
		ImGui::GetFont()->Scale = old_scale;
		ImGui::PopFont();
	}
	ImGui::End();
}

void renderer::ChartRenderer::drawCandlestick(const Candlestick& candle, ImDrawList* draw_list) {
	double center_x_ms{ static_cast<double>(candle.start_timestamp_) + (constants::CANDLESTICK_INTERVAL / 2.0) };

	double half_width_ms{ (constants::CANDLESTICK_INTERVAL * 0.45) };

	double left_x_ms = center_x_ms - half_width_ms;
	double right_x_ms = center_x_ms + half_width_ms;

	ImVec2 p_open = ImPlot::PlotToPixels(left_x_ms, candle.open_);
	ImVec2 p_close = ImPlot::PlotToPixels(right_x_ms, candle.close_);
	ImVec2 p_high = ImPlot::PlotToPixels(center_x_ms, candle.high_);
	ImVec2 p_low = ImPlot::PlotToPixels(center_x_ms, candle.low_);

	// green for up, red for down
	ImU32 color = (candle.close_ >= candle.open_)
		? IM_COL32(5, 101, 23, 255)
		: IM_COL32(222, 26, 36, 255);

	// draw the wick
	draw_list->AddLine(p_low, p_high, color, 10.0f);

	// draw the body
	float top_y{ std::min(p_open.y, p_close.y) };
	float bottom_y{ std::max(p_open.y, p_close.y) };

	// make sure the candle has minimum height so it shows up on flat markets
	if (bottom_y - top_y < 5.0f) {
		bottom_y = top_y + 5.0f;
	}

	draw_list->AddRectFilled(
		ImVec2(std::min(p_open.x, p_close.x), top_y),
		ImVec2(std::max(p_open.x, p_close.x), bottom_y),
		color
	);
}
