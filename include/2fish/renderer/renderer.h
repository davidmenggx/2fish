#pragma once

#include "2fish/models/market_snapshot.h"
#include "2fish/renderer/chart_renderer.h"
#include "2fish/utils/triple_buffer.h"

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

#include <atomic>
#include <memory>
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
		SDL_Renderer* main_renderer_{};
		SDL_Texture* main_texture_{};

		TripleBuffer<MarketSnapshot>& market_snapshot_buffer_;
		
		// TODO: is it more idiomatic to have the main renderer own the chart renderer
		// and initialize it post construction, or to have a unique pointer?
		std::unique_ptr<ChartRenderer> chart_renderer_;

		std::atomic<bool>& running_;
	};
}
