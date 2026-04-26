#pragma once

#include "2fish/models/market_snapshot.h"
#include "2fish/renderer/chart_renderer.h"
#include "2fish/utils/triple_buffer.h"

#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

#include "VkBootstrap.h"
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"
#include "implot.h"

#include <vector>
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
        void createRenderPass();
        void createFramebuffers();
        void createCommandAndSyncObjects();
        void recreateSwapchain();

        SDL_Window* main_window_{ nullptr };

        vkb::Instance vkb_inst_;
        vkb::Device vkb_device_;
        vkb::Swapchain vkb_swapchain_;

        VkSurfaceKHR surface_{ VK_NULL_HANDLE };
        VkQueue graphics_queue_{ VK_NULL_HANDLE };
        uint32_t graphics_queue_family_{ 0 };

        VkDescriptorPool descriptor_pool_{ VK_NULL_HANDLE };

        VkRenderPass render_pass_{ VK_NULL_HANDLE };
        std::vector<VkImage> swapchain_images_;
        std::vector<VkImageView> swapchain_image_views_;
        std::vector<VkFramebuffer> framebuffers_;

        VkCommandPool command_pool_{ VK_NULL_HANDLE };
        VkCommandBuffer command_buffer_{ VK_NULL_HANDLE };
        VkSemaphore image_available_semaphore_{ VK_NULL_HANDLE };
        VkSemaphore render_finished_semaphore_{ VK_NULL_HANDLE };
        VkFence in_flight_fence_{ VK_NULL_HANDLE };

		TripleBuffer<MarketSnapshot>& market_snapshot_buffer_;

		ChartRenderer chart_renderer_;

		std::atomic<bool>& running_;
	};
}
