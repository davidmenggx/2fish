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
	chart_texture_ = SDL_CreateTexture(main_renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, 200, 101);
}

void renderer::ChartRenderer::updateHeatmapHistory() {
	const MarketSnapshot* buffer{ market_snapshot_buffer_.getReaderBuffer() };

	std::array<RGBA, 101> heatmap_slice{};

	for (std::size_t i{ 0 }; i < buffer->bids_weight_.size(); ++i) {
		heatmap_slice[i] = heatmap_lookup_.getColor(buffer->bids_weight_[i]);
	}

	for (std::size_t i{ 0 }; i < buffer->asks_weight_.size(); ++i) {
		heatmap_slice[i] = heatmap_lookup_.getColor(buffer->asks_weight_[i]);
	}

	heatmap_history_.push(heatmap_slice);
}

void renderer::ChartRenderer::draw() {
	updateHeatmapHistory();

	void* locked_pixels{ nullptr };
	int pitch{ 0 };

	if (SDL_LockTexture(chart_texture_, nullptr, &locked_pixels, &pitch)) {
		std::size_t num_columns{ heatmap_history_.size() };
		for (std::size_t x{ 0 }; x < num_columns; ++x) {
			for (int y{ 0 }; y < 101; ++y) {
				auto* target_pixel = reinterpret_cast<RGBA*>(static_cast<uint8_t*>(locked_pixels) + (y * pitch) + (x * sizeof(RGBA)));
				*target_pixel = heatmap_history_[x][y];
			}

		}
		SDL_UnlockTexture(chart_texture_);
	}
	SDL_RenderTexture(main_renderer_, chart_texture_, nullptr, nullptr);
}
