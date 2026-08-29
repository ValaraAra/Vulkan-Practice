#include "renderer.h"

bool Renderer::initialize(uint32_t width, uint32_t height, std::string &outMessage) {
	outMessage = "Not implemented.";
	return false;
}

void Renderer::render() {}

void Renderer::shutdown() {}

bool Renderer::createVulkanInstance() { return false; }

bool Renderer::createSurface() { return false; }

VkPhysicalDevice Renderer::findPhysicalDevice() { return VK_NULL_HANDLE; }

bool Renderer::findGraphicsQueue() { return false; }

bool Renderer::createDevice(VkPhysicalDevice physicalDevice) { return false; }

bool Renderer::initializeVMA() { return false; }

bool Renderer::createSwapchain(uint32_t width, uint32_t height) { return false; }

void Renderer::destroySwapchain() {}

VkShaderModule Renderer::createShaderModule(const std::string &filename, shaderc_shader_kind kind) const {
	return VK_NULL_HANDLE;
}

bool Renderer::createShaders() { return false; }

VkPipeline Renderer::createGraphicsPipeline() { return VK_NULL_HANDLE; }

bool Renderer::createSyncResources() { return false; }

bool Renderer::createCommandBuffers() { return false; }
