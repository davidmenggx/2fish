#include "2fish/models/market_snapshot.h"
#include "2fish/renderer/chart_renderer.h"
#include "2fish/renderer/heatmap_lookup.h"
#include "2fish/utils/triple_buffer.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <array>
#include <cstdint>
renderer::ChartRenderer::ChartRenderer(SDL_Renderer* main_renderer, SDL_Texture* main_texture,
	TripleBuffer<MarketSnapshot>& market_snapshot_buffer)
	: main_renderer_{ main_renderer }, main_texture_{ main_texture }
	, market_snapshot_buffer_{ market_snapshot_buffer }
{
	chart_texture_ = SDL_CreateTexture(main_renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 200, 100);
}

void renderer::ChartRenderer::updateHeatmapHistory() {
	const MarketSnapshot* buffer{ market_snapshot_buffer_.getReaderBuffer() };

	std::array<RGBA, 100> heatmap_slice{};

	for (std::size_t i{ 0 }; i < 100; ++i) {
		uint8_t raw_weight{ std::max(buffer->asks_weight_[i], buffer->bids_weight_[i]) };
		uint8_t weight{ static_cast<uint8_t>(raw_weight) };

		heatmap_slice[i] = heatmap_lookup_.getColor(weight);
	}

	heatmap_history_.push(heatmap_slice);
}

void renderer::ChartRenderer::draw() {
	updateHeatmapHistory();

	void* locked_pixels{ nullptr };
	int pitch{ 0 };

	if (SDL_LockTexture(chart_texture_, nullptr, &locked_pixels, &pitch)) {
		std::size_t num_columns{ std::min(heatmap_history_.size(), static_cast<std::size_t>(200)) };
		for (std::size_t x{ 0 }; x < num_columns; ++x) {
			for (int y{ 0 }; y < 100; ++y) {
				auto* target_pixel = reinterpret_cast<RGBA*>(
					static_cast<uint8_t*>(locked_pixels) + (y * pitch) + (x * sizeof(RGBA)));
				*target_pixel = heatmap_history_[x][99 - y];
			}
		}
		SDL_UnlockTexture(chart_texture_);
	}
}
