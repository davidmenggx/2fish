#include "2fish/constants.h"
#include "2fish/market_store/market_store.h"
#include "2fish/models/candlestick.h"
#include "2fish/models/orderbook_snapshot.h"
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

void renderer::ChartRenderer::draw(const QueryResult& query) {
    ImGuiViewport* viewport{ ImGui::GetMainViewport() };
    ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.7f, viewport->WorkSize.y), ImGuiCond_Always);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("Orderbook heatmap", nullptr, window_flags)) {
        ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0, 0, 0, 1));
        ImPlot::PushStyleColor(ImPlotCol_PlotBg, ImVec4(0, 0, 0, 1));
        ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(0, 0));

        // 1. DETERMINE EXACT LOADING STATE
        bool is_completely_empty = true;
        for (std::size_t i{ 0 }; i < constants::HISTORY_STEPS; ++i) {
            if (query.snapshots_[i].candlestick_.close_ > 0 || query.snapshots_[i].candlestick_.high_ > 0) {
                is_completely_empty = false;
                break;
            }
        }

        bool is_fetching_history = !query.all_data_loaded_ && !is_completely_empty;

        // 2. ONLY USE GREYSCALE IF COMPLETELY EMPTY
        if (is_completely_empty) {
            ImPlot::PushColormap(ImPlotColormap_Greys);
        }
        else if (bookmap_colormap_ != -1) {
            ImPlot::PushColormap(bookmap_colormap_);
        }

        float old_scale = ImGui::GetFont()->Scale;
        ImGui::GetFont()->Scale *= 1.5f;
        ImGui::PushFont(ImGui::GetFont());

        ImPlotFlags plot_flags = ImPlotFlags_NoLegend | ImPlotFlags_NoMouseText | ImPlotFlags_NoTitle | ImPlotFlags_NoFrame;

        ImVec2 plot_pos;
        ImVec2 plot_size;

        if (ImPlot::BeginPlot("Orderbook", ImVec2(-1, -1), plot_flags)) {

            int y_min{ constants::PRICE_LEVELS };
            int y_max{ 0 };
            int last_trade_price{ 0 };

            for (std::size_t i{ 0 }; i < constants::HISTORY_STEPS; ++i) {
                const auto& candle = query.snapshots_[i].candlestick_;

                if (candle.high_ > 0 || candle.low_ > 0) {
                    y_min = std::min(y_min, candle.low_);
                    y_max = std::max(y_max, candle.high_);
                    if (last_trade_price == 0) last_trade_price = candle.close_; // Grab the most recent valid close
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

            ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_NoDecorations, ImPlotAxisFlags_Opposite);
            ImPlot::SetupAxisLimits(ImAxis_X1, 0, constants::HISTORY_STEPS, ImPlotCond_Always);
            ImPlot::SetupAxisLimits(ImAxis_Y1, y_min, y_max, ImPlotCond_Always);

            plot_pos = ImPlot::GetPlotPos();
            plot_size = ImPlot::GetPlotSize();

            ImDrawList* draw_list{ ImPlot::GetPlotDrawList() };
            ImPlot::PushPlotClipRect();

            // Draw last trade price line
            if (last_trade_price > 0) {
                float trade_y_pixels = ImPlot::PlotToPixels(0.0, static_cast<double>(last_trade_price)).y;
                for (float x{ plot_pos.x }; x < plot_pos.x + plot_size.x; x += 9.0f) {
                    ImVec2 start(x, trade_y_pixels);
                    ImVec2 end(std::min(x + 5.0f, plot_pos.x + plot_size.x), trade_y_pixels);

                    ImU32 line_col = is_completely_empty ? IM_COL32(150, 150, 150, 200) : IM_COL32(255, 165, 0, 200);
                    draw_list->AddLine(start, end, line_col, 3.0f);
                }
            }

            // 3. USE FULL COLOR IF WE HAVE AT LEAST SOME DATA
            ImU32 bull_col = is_completely_empty ? IM_COL32(120, 120, 120, 255) : IM_COL32(5, 101, 23, 255);
            ImU32 bear_col = is_completely_empty ? IM_COL32(80, 80, 80, 255) : IM_COL32(222, 26, 36, 255);
            ImU32 flat_col = is_completely_empty ? IM_COL32(100, 100, 100, 255) : IM_COL32(150, 150, 150, 255);

            for (std::size_t i{ 0 }; i < constants::HISTORY_STEPS; ++i) {
                const auto& candle = query.snapshots_[i].candlestick_;

                // Yes, skip completely empty candles so they don't draw weird artifacts at y=0
                if (candle.high_ == 0 && candle.low_ == 0 && candle.open_ == 0) {
                    continue;
                }

                drawCandlestick(candle, static_cast<double>(i) + 0.5, draw_list, bull_col, bear_col, flat_col);
            }

            // 4. SMART OVERLAYS
            if (is_completely_empty) {
                // Big center overlay if nothing has loaded yet
                const char* loading_text = "CONNECTING TO POLYMARKET...";
                ImVec2 text_size = ImGui::CalcTextSize(loading_text);
                ImVec2 text_pos = ImVec2(
                    plot_pos.x + (plot_size.x - text_size.x) * 0.5f,
                    plot_pos.y + (plot_size.y - text_size.y) * 0.5f
                );
                draw_list->AddRectFilled(
                    ImVec2(text_pos.x - 10, text_pos.y - 5),
                    ImVec2(text_pos.x + text_size.x + 10, text_pos.y + text_size.y + 5),
                    IM_COL32(0, 0, 0, 200)
                );
                draw_list->AddText(text_pos, IM_COL32(255, 255, 255, 255), loading_text);
            }
            else if (is_fetching_history) {
                // Small corner overlay indicating background fetching
                const char* fetching_text = "Fetching History...";
                ImVec2 text_pos = ImVec2(plot_pos.x + 10.0f, plot_pos.y + 10.0f);
                draw_list->AddText(text_pos, IM_COL32(200, 200, 200, 255), fetching_text);
            }

            ImPlot::PopPlotClipRect();
            ImPlot::EndPlot();
        }

        // ... (Button overlay logic remains exactly the same) ...

        // Ensure we pop the colormap if we pushed one
        if (is_completely_empty || bookmap_colormap_ != -1) ImPlot::PopColormap();

        ImPlot::PopStyleVar();
        ImPlot::PopStyleColor(2);
        ImGui::GetFont()->Scale = old_scale;
        ImGui::PopFont();
    }
    ImGui::End();
}

void renderer::ChartRenderer::drawCandlestick(const Candlestick& candle, double x_idx,
    ImDrawList* draw_list, ImU32 bull_col, ImU32 bear_col, ImU32 flat_col) {
    // TODO: move the colors to constants
    ImU32 color = flat_col;
    if (candle.close_ > candle.open_) {
        color = bull_col;
    }
    else if (candle.close_ < candle.open_) {
        color = bear_col;
    }

    float open_y = ImPlot::PlotToPixels(x_idx, candle.open_).y;
    float close_y = ImPlot::PlotToPixels(x_idx, candle.close_).y;
    float high_y = ImPlot::PlotToPixels(x_idx, candle.high_).y;
    float low_y = ImPlot::PlotToPixels(x_idx, candle.low_).y;

    float center_x_px = ImPlot::PlotToPixels(x_idx, candle.close_).x;

    // draw the wick
    draw_list->AddLine(ImVec2(center_x_px, low_y), ImVec2(center_x_px, high_y), color, 2.0f);

    // draw the body
    float half_width_px = 4.5f;
    float top_y = std::min(open_y, close_y);
    float bottom_y = std::max(open_y, close_y);

    if (bottom_y - top_y < 2.0f) {
        bottom_y = top_y + 2.0f;
    }

    draw_list->AddRectFilled(
        ImVec2(center_x_px - half_width_px, top_y),
        ImVec2(center_x_px + half_width_px, bottom_y),
        color
    );
}
