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
#include <memory>
#include <stdexcept>
#include <string>

renderer::Renderer::Renderer(TripleBuffer<MarketSnapshot>& market_snapshot_buffer,
	const std::string& title, int width, int height, std::atomic<bool>& running)
	: market_snapshot_buffer_{ market_snapshot_buffer }, running_{ running }
{
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		throw std::runtime_error(std::format("Could not initialize SDL: {}", SDL_GetError()));
	}

	if (!SDL_CreateWindowAndRenderer(title.c_str(), width, height, SDL_WINDOW_RESIZABLE, 
		&main_window_, &main_renderer_)) {
		throw std::runtime_error(std::format("Failed to construct window or renderer: {}", SDL_GetError()));
	}

	main_texture_ = SDL_CreateTexture(main_renderer_, SDL_PIXELFORMAT_RGBA32,
		SDL_TEXTUREACCESS_STREAMING, width, height);

	if (!main_texture_) {
		SDL_DestroyRenderer(main_renderer_);
		SDL_DestroyWindow(main_window_);
		SDL_Quit();
		throw std::runtime_error(std::format("Texture creation failed: {}", SDL_GetError()));
	}

	// TODO: create dear imgui
}

renderer::Renderer::~Renderer() {
	if (main_texture_) {
		SDL_DestroyTexture(main_texture_);
	}
	if (main_renderer_) {
		SDL_DestroyRenderer(main_renderer_);
	}
	if (main_window_) {
		SDL_DestroyWindow(main_window_);
	}
	SDL_Quit();

	// TODO: close dear imgui
}

void renderer::Renderer::run() {
	chart_renderer_ = std::make_unique<ChartRenderer>(main_renderer_, main_texture_, market_snapshot_buffer_);
	
	SDL_Event event;

	while (running_) {
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				running_.store(false);
			}
		}

		SDL_SetRenderDrawColor(main_renderer_, 0, 0, 0, 255);
		SDL_RenderClear(main_renderer_);

		chart_renderer_->draw();

		SDL_RenderTexture(main_renderer_, main_texture_, nullptr, nullptr);

		SDL_RenderPresent(main_renderer_);
	}
}
