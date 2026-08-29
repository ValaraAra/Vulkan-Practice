#pragma once
#include <cstdint>
#include <shaderc/shaderc.hpp>
#include <string>
#include <vulkan/vulkan.h>

class Renderer {
  public:
	bool initialize(uint32_t width, uint32_t height, std::string &outMessage);
	void render();
	void shutdown();

  private:
	bool createVulkanInstance();
	bool createSurface();
	VkPhysicalDevice findPhysicalDevice();
	bool findGraphicsQueue();
	bool createDevice(VkPhysicalDevice physicalDevice);
	bool initializeVMA();
	bool createSwapchain(uint32_t width, uint32_t height);
	void destroySwapchain();
	VkShaderModule createShaderModule(const std::string &filename, shaderc_shader_kind kind) const;
	bool createShaders();
	VkPipeline createGraphicsPipeline();
	bool createSyncResources();
	bool createCommandBuffers();
};