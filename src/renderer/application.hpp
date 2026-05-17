#pragma once

#include "engine/engine.hpp"
#include "component/component_manager.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <VkBootstrap.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"
#include "implot.h"

#include <atomic>
#include <cstdint>
#include <vector>

struct FrameData {
  VkCommandBuffer command_buffer;
  VkSemaphore image_available_semaphore;
  VkSemaphore render_finished_semaphore;
  VkFence in_flight_fence;
};

class Application {
public:
  Application(Engine &engine, std::atomic<bool> &running);
  ~Application();

  void run();

private:
  void createRenderPass();
  void createFramebuffers();
  void createCommandAndSyncObjects();
  void recreateSwapchain();

  Engine &engine_;

  SDL_Window *window_{nullptr};

  vkb::Instance vkb_inst_;
  vkb::Device vkb_device_;
  vkb::Swapchain vkb_swapchain_;

  VkSurfaceKHR surface_{VK_NULL_HANDLE};
  VkQueue graphics_queue_{VK_NULL_HANDLE};
  uint32_t graphics_queue_family_{0};

  VkDescriptorPool descriptor_pool_{VK_NULL_HANDLE};

  VkRenderPass render_pass_{VK_NULL_HANDLE};
  std::vector<VkImage> swapchain_images_;
  std::vector<VkImageView> swapchain_image_views_;
  std::vector<VkFramebuffer> framebuffers_;

  VkCommandPool command_pool_{VK_NULL_HANDLE};

  static constexpr uint32_t MAX_FRAMES_IN_FLIGHT{2};
  std::vector<FrameData> frames_;
  uint32_t current_frame_{0};
  bool swapchain_rebuild_{false};

  ComponentManager component_manager_{};

  std::atomic<bool> &running_;
};
