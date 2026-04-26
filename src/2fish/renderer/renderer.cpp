#include "2fish/models/market_snapshot.h"
#include "2fish/renderer/chart_renderer.h"
#include "2fish/renderer/renderer.h"
#include "2fish/utils/triple_buffer.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>

#include "VkBootstrap.h"
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"
#include "implot.h"

#include <atomic>
#include <cstdint>
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

	uint32_t window_flags{ SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN };
	main_window_ = SDL_CreateWindow(title.c_str(), width, height, window_flags);
	if (!main_window_) {
		throw std::runtime_error(std::format("Failed to construct window: {}", SDL_GetError()));
	}

	uint32_t ext_count{ 0 };
	const char* const* sdl_extensions{ SDL_Vulkan_GetInstanceExtensions(&ext_count) };

	vkb::InstanceBuilder builder;
	auto inst_ret = builder.set_app_name("2fish")
		.request_validation_layers(true) // TODO: turn off in prod
		.require_api_version(1, 2, 0)
		.enable_extensions(ext_count, sdl_extensions)
		.build();
	if (!inst_ret) {
		throw std::runtime_error("Failed to create Vulkan instance");
	}
	vkb_inst_ = inst_ret.value();

	if (!SDL_Vulkan_CreateSurface(main_window_, vkb_inst_.instance, nullptr, &surface_)) {
		throw std::runtime_error(std::format("Failed to create Vulkan surface: {}", SDL_GetError()));
	}

	vkb::PhysicalDeviceSelector selector{ vkb_inst_ };
	auto phys_ret = selector.set_surface(surface_)
		.set_minimum_version(1, 2)
		.select();
	if (!phys_ret) {
		throw std::runtime_error("Failed to select Vulkan Physical Device");
	}

	vkb::DeviceBuilder device_builder{ phys_ret.value() };
	auto dev_ret = device_builder.build();
	if (!dev_ret) {
		throw std::runtime_error("Failed to create Vulkan Device");
	}
	vkb_device_ = dev_ret.value();

	graphics_queue_ = vkb_device_.get_queue(vkb::QueueType::graphics).value();
	graphics_queue_family_ = vkb_device_.get_queue_index(vkb::QueueType::graphics).value();

	vkb::SwapchainBuilder swapchain_builder{ vkb_device_ };
	auto swap_ret = swapchain_builder.build();
	if (!swap_ret) {
		throw std::runtime_error("Failed to create Swapchain");
	}
	vkb_swapchain_ = swap_ret.value();

	swapchain_images_ = vkb_swapchain_.get_images().value();
	swapchain_image_views_ = vkb_swapchain_.get_image_views().value();

	createRenderPass();
	createFramebuffers();
	createCommandAndSyncObjects();

	VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 } };
	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = std::size(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;
	vkCreateDescriptorPool(vkb_device_.device, &pool_info, nullptr, &descriptor_pool_);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImPlot::CreateContext();
	ImGui::StyleColorsDark();

	chart_renderer_.init();

	ImGui_ImplSDL3_InitForVulkan(main_window_);

	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance = vkb_inst_.instance;
	init_info.PhysicalDevice = vkb_device_.physical_device;
	init_info.Device = vkb_device_.device;
	init_info.QueueFamily = graphics_queue_family_;
	init_info.Queue = graphics_queue_;
	init_info.DescriptorPool = descriptor_pool_;
	init_info.RenderPass = render_pass_;
	init_info.MinImageCount = vkb_swapchain_.image_count;
	init_info.ImageCount = vkb_swapchain_.image_count;
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	ImGui_ImplVulkan_Init(&init_info);
}

renderer::Renderer::~Renderer() {
	vkDeviceWaitIdle(vkb_device_.device);

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();

	vkDestroySemaphore(vkb_device_.device, image_available_semaphore_, nullptr);
	vkDestroySemaphore(vkb_device_.device, render_finished_semaphore_, nullptr);
	vkDestroyFence(vkb_device_.device, in_flight_fence_, nullptr);
	vkDestroyCommandPool(vkb_device_.device, command_pool_, nullptr);

	vkDestroyDescriptorPool(vkb_device_.device, descriptor_pool_, nullptr);

	for (auto framebuffer : framebuffers_) {
		vkDestroyFramebuffer(vkb_device_.device, framebuffer, nullptr);
	}

	vkDestroyRenderPass(vkb_device_.device, render_pass_, nullptr);

	vkb::destroy_swapchain(vkb_swapchain_);
	vkb::destroy_device(vkb_device_);
	vkDestroySurfaceKHR(vkb_inst_.instance, surface_, nullptr);
	vkb::destroy_instance(vkb_inst_);

	if (main_window_) {
		SDL_DestroyWindow(main_window_);
	}
	SDL_Quit();
}

void renderer::Renderer::createRenderPass() {
	VkAttachmentDescription color_attachment = {};
	color_attachment.format = vkb_swapchain_.image_format;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_attachment_ref = {};
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &color_attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	render_pass_info.dependencyCount = 1;
	render_pass_info.pDependencies = &dependency;

	if (vkCreateRenderPass(vkb_device_.device, &render_pass_info, nullptr, &render_pass_) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create Render Pass!");
	}
}

void renderer::Renderer::createFramebuffers() {
	framebuffers_.resize(swapchain_image_views_.size());

	for (size_t i = 0; i < swapchain_image_views_.size(); i++) {
		VkImageView attachments[] = {
			swapchain_image_views_[i]
		};

		VkFramebufferCreateInfo framebuffer_info = {};
		framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer_info.renderPass = render_pass_;
		framebuffer_info.attachmentCount = 1;
		framebuffer_info.pAttachments = attachments;
		framebuffer_info.width = vkb_swapchain_.extent.width;
		framebuffer_info.height = vkb_swapchain_.extent.height;
		framebuffer_info.layers = 1;

		if (vkCreateFramebuffer(vkb_device_.device, &framebuffer_info, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
			throw std::runtime_error(std::format("Failed to create Framebuffer for image {}", i));
		}
	}
}

void renderer::Renderer::createCommandAndSyncObjects() {
	VkCommandPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = graphics_queue_family_;

	if (vkCreateCommandPool(vkb_device_.device, &pool_info, nullptr, &command_pool_) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create command pool");
	}

	VkCommandBufferAllocateInfo alloc_info = {};
	alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc_info.commandPool = command_pool_;
	alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	alloc_info.commandBufferCount = 1;

	if (vkAllocateCommandBuffers(vkb_device_.device, &alloc_info, &command_buffer_) != VK_SUCCESS) {
		throw std::runtime_error("Failed to allocate command buffer");
	}

	VkSemaphoreCreateInfo semaphore_info = {};
	semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fence_info = {};
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	if (vkCreateSemaphore(vkb_device_.device, &semaphore_info, nullptr, &image_available_semaphore_) != VK_SUCCESS ||
		vkCreateSemaphore(vkb_device_.device, &semaphore_info, nullptr, &render_finished_semaphore_) != VK_SUCCESS ||
		vkCreateFence(vkb_device_.device, &fence_info, nullptr, &in_flight_fence_) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create sync objects");
	}
}

void renderer::Renderer::run() {
	SDL_Event event;

	while (running_) {
		while (SDL_PollEvent(&event)) {
			ImGui_ImplSDL3_ProcessEvent(&event);
			if (event.type == SDL_EVENT_QUIT) {
				running_.store(false, std::memory_order_relaxed);
			}
		}

		vkWaitForFences(vkb_device_.device, 1, &in_flight_fence_, VK_TRUE, UINT64_MAX);

		uint32_t image_index;
		VkResult result = vkAcquireNextImageKHR(vkb_device_.device, vkb_swapchain_.swapchain, UINT64_MAX,
			image_available_semaphore_, VK_NULL_HANDLE, &image_index);

		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			recreateSwapchain();
			continue;
		}
		else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			throw std::runtime_error("Failed to acquire swapchain image!");
		}

		vkResetFences(vkb_device_.device, 1, &in_flight_fence_);

		vkResetCommandBuffer(command_buffer_, 0);

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		// build the charts
		const MarketSnapshot* snapshot{ market_snapshot_buffer_.getReaderBuffer() };
		chart_renderer_.updateAndDraw(snapshot);

		ImGui::Render();
		ImDrawData* draw_data = ImGui::GetDrawData();

		VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		vkBeginCommandBuffer(command_buffer_, &begin_info);

		VkRenderPassBeginInfo render_pass_info = {};
		render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		render_pass_info.renderPass = render_pass_;
		render_pass_info.framebuffer = framebuffers_[image_index];
		render_pass_info.renderArea.offset = { 0, 0 };
		render_pass_info.renderArea.extent = vkb_swapchain_.extent;

		VkClearValue clear_color = { {{0.0f, 0.0f, 0.0f, 1.0f}} };
		render_pass_info.clearValueCount = 1;
		render_pass_info.pClearValues = &clear_color;

		vkCmdBeginRenderPass(command_buffer_, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

		ImGui_ImplVulkan_RenderDrawData(draw_data, command_buffer_);

		vkCmdEndRenderPass(command_buffer_);
		vkEndCommandBuffer(command_buffer_);

		// vulkan submit
		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore wait_semaphores[] = { image_available_semaphore_ };
		VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submit_info.waitSemaphoreCount = 1;
		submit_info.pWaitSemaphores = wait_semaphores;
		submit_info.pWaitDstStageMask = wait_stages;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &command_buffer_;

		VkSemaphore signal_semaphores[] = { render_finished_semaphore_ };
		submit_info.signalSemaphoreCount = 1;
		submit_info.pSignalSemaphores = signal_semaphores;

		vkQueueSubmit(graphics_queue_, 1, &submit_info, in_flight_fence_);

		VkPresentInfoKHR present_info = {};
		present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		present_info.waitSemaphoreCount = 1;
		present_info.pWaitSemaphores = signal_semaphores;

		VkSwapchainKHR swapchains[] = { vkb_swapchain_.swapchain };
		present_info.swapchainCount = 1;
		present_info.pSwapchains = swapchains;
		present_info.pImageIndices = &image_index;

		result = vkQueuePresentKHR(graphics_queue_, &present_info);

		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
			recreateSwapchain();
		}
		else if (result != VK_SUCCESS) {
			throw std::runtime_error("Failed to present swapchain image");
		}
	}
}
void renderer::Renderer::recreateSwapchain() {
	int width{ 0 };
	int height{ 0 };
	SDL_GetWindowSizeInPixels(main_window_, &width, &height);

	while (width == 0 || height == 0) {
		SDL_GetWindowSizeInPixels(main_window_, &width, &height);
		SDL_Event event;
		SDL_WaitEvent(&event);
	}

	vkDeviceWaitIdle(vkb_device_.device);

	for (auto framebuffer : framebuffers_) {
		vkDestroyFramebuffer(vkb_device_.device, framebuffer, nullptr);
	}
	framebuffers_.clear();

	for (auto imageView : swapchain_image_views_) {
		vkDestroyImageView(vkb_device_.device, imageView, nullptr);
	}
	swapchain_image_views_.clear();

	vkb::SwapchainBuilder swapchain_builder{ vkb_device_ };
	auto vkb_swapchain_ret = swapchain_builder
		.set_old_swapchain(vkb_swapchain_)
		.build();

	if (!vkb_swapchain_ret) {
		throw std::runtime_error("Failed to recreate swapchain!");
	}

	vkb::destroy_swapchain(vkb_swapchain_);
	vkb_swapchain_ = vkb_swapchain_ret.value();

	auto views_ret = vkb_swapchain_.get_image_views();
	if (!views_ret) {
		throw std::runtime_error("Failed to get new swapchain image views!");
	}
	swapchain_image_views_ = views_ret.value();

	createFramebuffers();
}
