#pragma once

#include <stdexcept>
#include <vector>
#define VK_NO_PROTOTYPES
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <shaderc/shaderc.hpp>
#include <string>
#include <vulkan/vulkan.h>

struct VmaAllocator_T;
typedef struct VmaAllocator_T* VmaAllocator;
struct VmaAllocation_T;
typedef struct VmaAllocation_T* VmaAllocation;

struct FrameResources
{
	VkCommandPool commandPool{VK_NULL_HANDLE};
	VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
	VkSemaphore imageAcquiredSemaphore{VK_NULL_HANDLE};
};

class RenderError : public std::runtime_error
{
  public:
	using std::runtime_error::runtime_error;
};

class Renderer
{
  public:
	void initialize(SDL_Window* window);
	void render();
	void shutdown();

	void invalidateSwapchain();

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

	VkDebugUtilsMessengerEXT debugMessenger{VK_NULL_HANDLE};

	SDL_Window* window{nullptr};

	uint64_t frameIndex{0};
	uint64_t nextSignalValue{MaxFramesInFlight + 1};

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

	VkShaderModule vertShader{VK_NULL_HANDLE};
	VkShaderModule fragShader{VK_NULL_HANDLE};

	VkSemaphore timelineSemaphore{VK_NULL_HANDLE};
	std::array<FrameResources, MaxFramesInFlight> frameResources;

	void createVulkanInstance();
	void createSurface();
	VkPhysicalDevice selectPhysicalDevice();
	void selectGraphicsQueue();
	void createDevice(VkPhysicalDevice physicalDevice);
	void initializeVMA();
	void createSwapchain();
	void destroySwapchain();
	VkShaderModule createShaderModule(const std::string& filename, shaderc_shader_kind kind) const;
	void createShaders();
	VkPipeline createGraphicsPipeline();
	void createSyncResources();
	void createCommandBuffers();
	void recreateImageAcquiredSemaphore(FrameResources& frameResource);
};