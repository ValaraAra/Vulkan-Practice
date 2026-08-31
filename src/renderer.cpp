#include "renderer.h"
#define VOLK_IMPLEMENTATION
#include <Volk/volk.h>
#define VMA_IMPLEMENTATION
#include <SDL3/SDL_vulkan.h>
#include <iostream>
#include <vma/vk_mem_alloc.h>

// Debug callback for Vulkan validation layers
VKAPI_ATTR VkBool32 VKAPI_CALL Renderer::debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData)
{
	if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
		std::cerr << "Vulkan validation layer: " << pCallbackData->pMessage << std::endl;
	}

	return VK_FALSE;
}

bool Renderer::initialize(SDL_Window *window, std::string &outMessage)
{
	// Initialize Volk (Vulkan function loader)
	if (volkInitialize() != VK_SUCCESS) {
		outMessage = "Error initializing Volk.";
		return false;
	}

	// Create Vulkan instance
	if (!createVulkanInstance()) {
		outMessage = "Error creating Vulkan instance.";
		return false;
	}

	// Load Vulkan functions for the new instance
	volkLoadInstance(vulkanInstance);

	outMessage = "Not implemented.";
	return false;
}

void Renderer::render() {}

void Renderer::shutdown() {}

bool Renderer::createVulkanInstance()
{
	VkApplicationInfo applicationInfo{
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pNext = nullptr,
		.pApplicationName = "Vulkan Practice",
		.apiVersion = VulkanAPIVersion,
	};

	// Extensions
	uint32_t instanceExtensionCount = 0;
	const char *const *extensions = SDL_Vulkan_GetInstanceExtensions(&instanceExtensionCount);
	std::vector<const char *> requestedExtensions{
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
	};
	for (uint32_t i = 0; i < instanceExtensionCount; ++i) {
		requestedExtensions.push_back(extensions[i]);
	}

	// Layers
	std::vector<const char *> requestedLayers{
		"VK_LAYER_KHRONOS_validation",
	};

	VkDebugUtilsMessengerCreateInfoEXT debugInfo{.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
						   VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = debugCallback};

	VkInstanceCreateInfo instanceCreateInfo{
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext = &debugInfo,
		.pApplicationInfo = &applicationInfo,
		.enabledLayerCount = static_cast<uint32_t>(requestedLayers.size()),
		.ppEnabledLayerNames = requestedLayers.data(),
		.enabledExtensionCount = static_cast<uint32_t>(requestedExtensions.size()),
		.ppEnabledExtensionNames = requestedExtensions.data(),
	};

	return vkCreateInstance(&instanceCreateInfo, nullptr, &vulkanInstance) == VK_SUCCESS;
}

bool Renderer::createSurface() { return false; }

VkPhysicalDevice Renderer::findPhysicalDevice() { return VK_NULL_HANDLE; }

bool Renderer::findGraphicsQueue() { return false; }

bool Renderer::createDevice(VkPhysicalDevice physicalDevice) { return false; }

bool Renderer::initializeVMA() { return false; }

bool Renderer::createSwapchain(uint32_t width, uint32_t height) { return false; }

void Renderer::destroySwapchain() {}

VkShaderModule Renderer::createShaderModule(const std::string &filename, shaderc_shader_kind kind) const
{
	return VK_NULL_HANDLE;
}

bool Renderer::createShaders() { return false; }

VkPipeline Renderer::createGraphicsPipeline() { return VK_NULL_HANDLE; }

bool Renderer::createSyncResources() { return false; }

bool Renderer::createCommandBuffers() { return false; }
