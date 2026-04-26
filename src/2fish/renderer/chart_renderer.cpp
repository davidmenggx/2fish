#include "2fish/models/market_snapshot.h"
#include "2fish/renderer/chart_renderer.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <iterator>

renderer::ChartRenderer::ChartRenderer()
	: heatmap_render_buffer_(kHistorySteps* kPriceLevels, 0.0)
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
}

void renderer::ChartRenderer::updateAndDraw(const MarketSnapshot* snapshot) {
	if (snapshot) {
		double current_time{ ImGui::GetTime() };

		// TODO: do not hard code time interval
		if (current_time - last_shift_time_ >= 0.25) {
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

	ImGuiViewport* viewport = ImGui::GetMainViewport();

	const float panel_width = viewport->WorkSize.x * 0.7f;
	const float panel_height = viewport->WorkSize.y;

	// anchor to top left of screen
	ImVec2 window_pos = viewport->WorkPos;

	ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(panel_width, panel_height), ImGuiCond_Always);

	ImGuiWindowFlags window_flags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoBackground;

	if (ImGui::Begin("Orderbook heatmap", nullptr, window_flags)) {
		ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
		ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
		ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0.0f, 0.0f));

		if (bookmap_colormap_ != -1) {
			ImPlot::PushColormap(bookmap_colormap_);
		}

		ImPlotFlags plot_flags = ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText | ImPlotFlags_NoTitle | ImPlotFlags_NoFrame;

		if (ImPlot::BeginPlot("##OrderBook", ImVec2(-1, -1), plot_flags)) {

			ImPlotAxisFlags x_flags = ImPlotAxisFlags_NoDecorations;

			// Removed ImPlotAxisFlags_Opposite so the ticks stay on the left
			ImPlotAxisFlags y_flags = ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoLabel;

			ImPlot::SetupAxes(nullptr, nullptr, x_flags, y_flags);

			ImPlot::PushStyleColor(ImPlotCol_AxisText, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));

			ImPlot::SetupAxisLimits(ImAxis_X1, 0, kHistorySteps, ImPlotCond_Always);
			ImPlot::SetupAxisLimits(ImAxis_Y1, 0, kPriceLevels, ImPlotCond_Always);

			ImPlot::PlotHeatmap("Liquidity",
				heatmap_render_buffer_.data(),
				kPriceLevels,
				kHistorySteps,
				0.0, 1.0,
				nullptr,
				ImPlotPoint(0, 0),
				ImPlotPoint(kHistorySteps, kPriceLevels));

			ImPlot::PopStyleColor();
			ImPlot::EndPlot();
		}

		if (bookmap_colormap_ != -1) {
			ImPlot::PopColormap();
		}

		ImPlot::PopStyleVar();
		ImPlot::PopStyleColor(2);
	}
	ImGui::End();
}
