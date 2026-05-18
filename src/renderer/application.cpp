#include "application.hpp"
#include "component/chart_view.hpp"
#include "component/market_depth.hpp"
#include "component/orderbook_levels.hpp"
#include "component/trade_ledger.hpp"
#include "engine/engine.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"
#include "implot.h"

#include <atomic>
#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <stdexcept>

Application::Application(Engine &engine, std::atomic<bool> &running)
    : engine_{engine}, running_{running} {
  if (!SDL_Init(SDL_INIT_VIDEO))
    throw std::runtime_error(
        std::format("Could not initialize SDL: {}", SDL_GetError()));

  uint32_t window_flags{SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN |
                        SDL_WINDOW_HIGH_PIXEL_DENSITY};
  // TODO: better way of setting length & width
  window_ = SDL_CreateWindow("2fish Terminal", 1920, 1080, window_flags);
  if (!window_)
    throw std::runtime_error(
        std::format("Failed to construct window: {}", SDL_GetError()));

  uint32_t ext_count{0};
  const char *const *sdl_extensions{
      SDL_Vulkan_GetInstanceExtensions(&ext_count)};

  vkb::InstanceBuilder builder;
  auto inst_ret = builder.set_app_name("2fish")
                      .request_validation_layers(true) // TODO: turn off in prod
                      .require_api_version(1, 2, 0)
                      .enable_extensions(ext_count, sdl_extensions)
                      .build();
  if (!inst_ret)
    throw std::runtime_error("Failed to create Vulkan instance");

  vkb_inst_ = inst_ret.value();

  if (!SDL_Vulkan_CreateSurface(window_, vkb_inst_.instance, nullptr,
                                &surface_))
    throw std::runtime_error(
        std::format("Failed to create Vulkan surface: {}", SDL_GetError()));

  vkb::PhysicalDeviceSelector selector{vkb_inst_};
  auto phys_ret =
      selector.set_surface(surface_).set_minimum_version(1, 2).select();
  if (!phys_ret)
    throw std::runtime_error("Failed to select Vulkan Physical Device");

  vkb::DeviceBuilder device_builder{phys_ret.value()};
  auto dev_ret = device_builder.build();
  if (!dev_ret)
    throw std::runtime_error("Failed to create Vulkan Device");
  vkb_device_ = dev_ret.value();

  graphics_queue_ = vkb_device_.get_queue(vkb::QueueType::graphics).value();
  graphics_queue_family_ =
      vkb_device_.get_queue_index(vkb::QueueType::graphics).value();

  vkb::SwapchainBuilder swapchain_builder{vkb_device_};
  swapchain_builder.set_desired_format(VkSurfaceFormatKHR{
      VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
  auto swap_ret = swapchain_builder.build();
  if (!swap_ret)
    throw std::runtime_error("Failed to create Swapchain");
  vkb_swapchain_ = swap_ret.value();

  swapchain_images_ = vkb_swapchain_.get_images().value();
  swapchain_image_views_ = vkb_swapchain_.get_image_views().value();

  createRenderPass();
  createFramebuffers();
  createCommandAndSyncObjects();

  VkDescriptorPoolSize pool_sizes[] = {
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2048}};
  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 1000;
  pool_info.poolSizeCount = std::size(pool_sizes);
  pool_info.pPoolSizes = pool_sizes;
  vkCreateDescriptorPool(vkb_device_.device, &pool_info, nullptr,
                         &descriptor_pool_);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

  ImGui::StyleColorsDark();

  ImGuiStyle &style = ImGui::GetStyle();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
  }

  ImGui_ImplSDL3_InitForVulkan(window_);

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

  io.Fonts->AddFontFromFileTTF("assets/fonts/JetBrainsMono-Regular.ttf", 24.0f);

  // Testing for now
  component_manager_.addComponent(std::make_unique<OrderbookLevels>(engine_));
  component_manager_.addComponent(std::make_unique<TradeLedger>(engine_));
  component_manager_.addComponent(std::make_unique<MarketDepth>(engine_));
  component_manager_.addComponent(std::make_unique<ChartView>(engine_));
}

Application::~Application() {
  // Wait for the GPU
  vkDeviceWaitIdle(vkb_device_.device);

  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();

  for (const auto &frame : frames_) {
    vkDestroySemaphore(vkb_device_.device, frame.image_available_semaphore,
                       nullptr);
    vkDestroySemaphore(vkb_device_.device, frame.render_finished_semaphore,
                       nullptr);
    vkDestroyFence(vkb_device_.device, frame.in_flight_fence, nullptr);
  }

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

  if (window_) {
    SDL_DestroyWindow(window_);
  }
  SDL_Quit();
}

void Application::createRenderPass() {
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

  if (vkCreateRenderPass(vkb_device_.device, &render_pass_info, nullptr,
                         &render_pass_) != VK_SUCCESS)
    throw std::runtime_error("Failed to create Render Pass");
}

void Application::createFramebuffers() {
  framebuffers_.resize(swapchain_image_views_.size());

  for (std::size_t i{0}; i < swapchain_image_views_.size(); i++) {
    VkImageView attachments[] = {swapchain_image_views_[i]};

    VkFramebufferCreateInfo framebuffer_info = {};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = render_pass_;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = attachments;
    framebuffer_info.width = vkb_swapchain_.extent.width;
    framebuffer_info.height = vkb_swapchain_.extent.height;
    framebuffer_info.layers = 1;

    if (vkCreateFramebuffer(vkb_device_.device, &framebuffer_info, nullptr,
                            &framebuffers_[i]) != VK_SUCCESS) {
      throw std::runtime_error(
          std::format("Failed to create Framebuffer for image {}", i));
    }
  }
}

void Application::createCommandAndSyncObjects() {
  VkCommandPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = graphics_queue_family_;

  if (vkCreateCommandPool(vkb_device_.device, &pool_info, nullptr,
                          &command_pool_) != VK_SUCCESS)
    throw std::runtime_error("Failed to create command pool");

  frames_.resize(MAX_FRAMES_IN_FLIGHT);

  VkCommandBufferAllocateInfo alloc_info = {};
  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.commandPool = command_pool_;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandBufferCount = 1;

  VkSemaphoreCreateInfo semaphore_info = {};
  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fence_info = {};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (uint32_t i{0}; i < MAX_FRAMES_IN_FLIGHT; i++) {
    if (vkAllocateCommandBuffers(vkb_device_.device, &alloc_info,
                                 &frames_[i].command_buffer) != VK_SUCCESS)
      throw std::runtime_error("Failed to allocate command buffer");

    if (vkCreateSemaphore(vkb_device_.device, &semaphore_info, nullptr,
                          &frames_[i].image_available_semaphore) !=
            VK_SUCCESS ||
        vkCreateSemaphore(vkb_device_.device, &semaphore_info, nullptr,
                          &frames_[i].render_finished_semaphore) !=
            VK_SUCCESS ||
        vkCreateFence(vkb_device_.device, &fence_info, nullptr,
                      &frames_[i].in_flight_fence) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create sync objects");
    }
  }
}

void Application::recreateSwapchain() {
  int width{0};
  int height{0};
  SDL_GetWindowSizeInPixels(window_, &width, &height);

  while (width == 0 || height == 0) {
    SDL_GetWindowSizeInPixels(window_, &width, &height);
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

  vkb::SwapchainBuilder swapchain_builder{vkb_device_};
  auto vkb_swapchain_ret =
      swapchain_builder.set_old_swapchain(vkb_swapchain_)
          .set_desired_format(VkSurfaceFormatKHR{
              VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
          .build();

  if (!vkb_swapchain_ret)
    throw std::runtime_error("Failed to recreate swapchain");

  vkb::destroy_swapchain(vkb_swapchain_);
  vkb_swapchain_ = vkb_swapchain_ret.value();

  auto views_ret = vkb_swapchain_.get_image_views();
  if (!views_ret)
    throw std::runtime_error("Failed to get new swapchain image views");
  swapchain_image_views_ = views_ret.value();

  createFramebuffers();
}

void Application::run() {
  ImGuiIO &io = ImGui::GetIO();
  SDL_Event event;

  while (running_.load(std::memory_order_relaxed)) {
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) {
        running_.store(false, std::memory_order_relaxed);
      }
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
          event.window.windowID == SDL_GetWindowID(window_)) {
        running_.store(false, std::memory_order_relaxed);
      }
      // Rebuild the swapchain if the window size changes
      if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        swapchain_rebuild_ = true;
      }
    }

    if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) {
      SDL_Delay(10);
      continue;
    }

    if (swapchain_rebuild_) {
      recreateSwapchain();
      swapchain_rebuild_ = false;
    }

    FrameData &fd = frames_[current_frame_];
    vkWaitForFences(vkb_device_.device, 1, &fd.in_flight_fence, VK_TRUE,
                    UINT64_MAX);

    uint32_t image_index;
    VkResult result = vkAcquireNextImageKHR(
        vkb_device_.device, vkb_swapchain_.swapchain, UINT64_MAX,
        fd.image_available_semaphore, VK_NULL_HANDLE, &image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
      swapchain_rebuild_ = true;
      continue; // Skip this frame and try again
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
      throw std::runtime_error("Failed to acquire swapchain image");
    }

    vkResetFences(vkb_device_.device, 1, &fd.in_flight_fence);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    component_manager_.drawAll();

    ImGui::Render();
    ImDrawData *draw_data{ImGui::GetDrawData()};

    vkResetCommandBuffer(fd.command_buffer, 0);
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(fd.command_buffer, &begin_info);

    VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkRenderPassBeginInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = render_pass_;
    render_pass_info.framebuffer = framebuffers_[image_index];
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = vkb_swapchain_.extent;
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_color;

    vkCmdBeginRenderPass(fd.command_buffer, &render_pass_info,
                         VK_SUBPASS_CONTENTS_INLINE);

    ImGui_ImplVulkan_RenderDrawData(draw_data, fd.command_buffer);

    vkCmdEndRenderPass(fd.command_buffer);
    vkEndCommandBuffer(fd.command_buffer);

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore wait_semaphores[] = {fd.image_available_semaphore};
    VkPipelineStageFlags wait_stages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &fd.command_buffer;

    VkSemaphore signal_semaphores[] = {fd.render_finished_semaphore};
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    if (vkQueueSubmit(graphics_queue_, 1, &submit_info, fd.in_flight_fence) !=
        VK_SUCCESS)
      throw std::runtime_error("Failed to submit draw command buffer");

    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;
    VkSwapchainKHR swapchains[] = {vkb_swapchain_.swapchain};
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swapchains;
    present_info.pImageIndices = &image_index;

    result = vkQueuePresentKHR(graphics_queue_, &present_info);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
      swapchain_rebuild_ = true;
    else if (result != VK_SUCCESS)
      throw std::runtime_error("Failed to present swapchain image");

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
      ImGui::UpdatePlatformWindows();
      ImGui::RenderPlatformWindowsDefault();
    }

    current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT;
  }
  std::cout << "Application frontend down\n";
}
