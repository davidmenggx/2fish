#pragma once

#include "2fish/models/market_snapshot.h"
#include "2fish/renderer/chart_renderer.h"
#include "2fish/utils/triple_buffer.h"

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

#include <atomic>
#include <string>

namespace renderer {
	class Renderer {
	public:
		Renderer(TripleBuffer<MarketSnapshot>& market_snapshot_buffer,
			const std::string& title, int width, int height, std::atomic<bool>& running);

		~Renderer();

		void run();

	private:
		SDL_Window* main_window_{};

		TripleBuffer<MarketSnapshot>& market_snapshot_buffer_;

		ChartRenderer chart_renderer_;

		std::atomic<bool>& running_;
	};
}
