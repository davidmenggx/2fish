#pragma once

#include "2fish/models/market_snapshot.h"
#include "2fish/renderer/heatmap_lookup.h"
#include "2fish/utils/ring_buffer.h"
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

#include <array>
#include <cstdint>

namespace renderer {
	class ChartRenderer {
	public:
		ChartRenderer(SDL_Renderer* main_renderer, SDL_Texture* main_texture, 
			TripleBuffer<MarketSnapshot>& market_snapshot_buffer);

		void draw();

	private:
		void updateHeatmapHistory();

		SDL_Renderer* main_renderer_{};
		SDL_Texture* main_texture_{};
		SDL_Texture* chart_texture_{}; // Streaming texture owned by the chart renderer

		TripleBuffer<MarketSnapshot>& market_snapshot_buffer_;

		uint16_t window_width_{};
		uint16_t window_height_{};

		HeatmapLookup heatmap_lookup_{};
		RingBuffer<std::array<RGBA, 101>, 128U> heatmap_history_{};
	};
}
