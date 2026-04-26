#include "2fish/models/market_snapshot.h"
#include "2fish/renderer/chart_renderer.h"
#include "2fish/renderer/renderer.h"
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

#include <atomic>
#include <format>
#include <stdexcept>
#include <string>

renderer::Renderer::Renderer(TripleBuffer<MarketSnapshot>& market_snapshot_buffer,
	const std::string& title, int width, int height, std::atomic<bool>& running)
	: market_snapshot_buffer_{ market_snapshot_buffer }, running_{ running }
{
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		throw std::runtime_error(std::format("Could not initialize SDL: {}", SDL_GetError()));
	}

	if (!SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_RESIZABLE)) {
		throw std::runtime_error(std::format("Failed to construct window: {}", SDL_GetError()));
	}

	// TODO: create dear imgui
}

renderer::Renderer::~Renderer() {
	if (main_window_) {
		SDL_DestroyWindow(main_window_);
	}
	SDL_Quit();

	// TODO: close dear imgui
}

void renderer::Renderer::run() {	
	SDL_Event event;

	while (running_) {
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				running_.store(false, std::memory_order_relaxed);
			}
		}

		const MarketSnapshot* snapshot{ market_snapshot_buffer_.getReaderBuffer() };

		chart_renderer_.updateAndDraw(snapshot);
	}
}
