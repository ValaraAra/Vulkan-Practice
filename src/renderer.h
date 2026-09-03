#pragma once

#include <vector>
#define VK_NO_PROTOTYPES
#include <SDL3/SDL.h>
#include <cstdint>
#include <functional>
#include <shaderc/shaderc.hpp>
#include <string>
#include <vulkan/vulkan.h>

struct VmaAllocator_T;
typedef struct VmaAllocator_T* VmaAllocator;
struct VmaAllocation_T;
typedef struct VmaAllocation_T* VmaAllocation;

class Renderer
{
  public:
	bool initialize(SDL_Window* window, std::function<void(const std::string&)> errorCallback);
	void render();
	void shutdown();

  private:
	constexpr static uint32_t VulkanAPIVersion{VK_API_VERSION_1_4};
	constexpr static uint32_t MaxFramesInFlight{2};
	constexpr static VkFormat swapchainFormat{VK_FORMAT_B8G8R8A8_SRGB};
	constexpr static VkFormat depthFormat{VK_FORMAT_D32_SFLOAT};

	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData
	);

	SDL_Window* window{nullptr};
	std::function<void(const std::string&)> errorCallback;

	VkInstance vulkanInstance{VK_NULL_HANDLE};
	VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
	VkDevice device{VK_NULL_HANDLE};
	VkSurfaceKHR surface{VK_NULL_HANDLE};
	VmaAllocator vmaAllocator{VK_NULL_HANDLE};

	uint32_t graphicsQueueFamilyIndex{UINT32_MAX};
	VkQueue graphicsQueue{VK_NULL_HANDLE};

	VkSwapchainKHR swapchain{VK_NULL_HANDLE};
	std::vector<VkImage> swapchainImages;
	std::vector<VkImageView> swapchainImageViews;
	std::vector<VkSemaphore> renderCompleteSemaphores;
	bool requireSwapchainRecreation{false};
	uint32_t swapchainWidth{0};
	uint32_t swapchainHeight{0};

	VkImage depthImage{VK_NULL_HANDLE};
	VkImageView depthImageView{VK_NULL_HANDLE};
	VmaAllocation depthImageAllocation{VK_NULL_HANDLE};

	VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
	VkPipeline pipeline{VK_NULL_HANDLE};

	bool createVulkanInstance();
	bool createSurface();
	VkPhysicalDevice selectPhysicalDevice();
	bool selectGraphicsQueue();
	bool createDevice(VkPhysicalDevice physicalDevice);
	bool initializeVMA();
	bool createSwapchain(uint32_t width, uint32_t height);
	void destroySwapchain();
	VkShaderModule createShaderModule(const std::string& filename, shaderc_shader_kind kind) const;
	bool createShaders();
	VkPipeline createGraphicsPipeline();
	bool createSyncResources();
	bool createCommandBuffers();
};