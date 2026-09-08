#include "renderer.h"

#include "utility.h"

#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <stb_image.h>
#include <unordered_map>
#include <vector>

#define VOLK_IMPLEMENTATION
#include <Volk/volk.h>

#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

void Renderer::initialize(SDL_Window* sdlWindow)
{
	window = sdlWindow;

	scene.initialize(1024);
	nodeRenderStack.reserve(128);

	if (volkInitialize() != VK_SUCCESS) { throw RenderError("Error initializing Volk."); }

	createVulkanInstance();
	createSurface();
	physicalDevice = selectPhysicalDevice();
	selectGraphicsQueue();
	createDevice();
	initializeVMA();
	createSwapchain();
	createShaders();
	pipeline = createGraphicsPipeline();
	createSyncResources();
	createCommandBuffers();
	createFallbackTexture();
}

void Renderer::loadData(const std::string& path)
{
	// Only a simple gltf model for now
	loadGLTF(path);
}

void Renderer::render()
{
	// Check swapchain validity
	if (requireSwapchainRecreation)
	{
		vkDeviceWaitIdle(device);
		destroySwapchain();
		createSwapchain();
		requireSwapchainRecreation = false;
	}

	// Determine frame resource index and timeline semaphore values
	const uint32_t frameResourceIndex = frameIndex % MaxFramesInFlight;
	const uint64_t signalValue = nextSignalValue;
	const uint64_t waitValue = signalValue - MaxFramesInFlight;

	// Ensure it's safe to start recording commands for this frame resource
	VkSemaphoreWaitInfo waitInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
		.semaphoreCount = 1,
		.pSemaphores = &timelineSemaphore,
		.pValues = &waitValue,
	};
	vkWaitSemaphores(device, &waitInfo, UINT64_MAX);

	// Reset the command pool for this frame resource
	FrameResources& frameResource = frameResources[frameResourceIndex];
	vkResetCommandPool(device, frameResource.commandPool, 0);

	// Acquire next swapchain image
	VkSemaphore imageAcquiredSemaphore = frameResource.imageAcquiredSemaphore;

	uint32_t swapchainImageIndex;
	VkResult acquireResult =
		vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAcquiredSemaphore, VK_NULL_HANDLE, &swapchainImageIndex);

	// Handle swapchain recreation if needed
	if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR)
	{
		recreateImageAcquiredSemaphore(frameResource);
		requireSwapchainRecreation = true;
		return;
	}
	else if (acquireResult != VK_SUCCESS) { throw RenderError("Failed to acquire next swapchain image."); }

	// Image acquired, increment frame index and timeline signal value
	++frameIndex;
	++nextSignalValue;

	// Begin recording commands into the command buffer for this frame resource
	VkCommandBufferBeginInfo commandBufferBeginInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	vkBeginCommandBuffer(frameResource.commandBuffer, &commandBufferBeginInfo);

	// Transition the color and depth images
	std::vector<VkImageMemoryBarrier2> imageBarriers{
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.srcAccessMask = 0,
			.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.image = swapchainImages[swapchainImageIndex],
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			},
		},
		{
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
			.srcAccessMask = 0,
			.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			.image = depthImage,
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			},
		}
	};

	VkDependencyInfo dependencyInfo{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = static_cast<uint32_t>(imageBarriers.size()),
		.pImageMemoryBarriers = imageBarriers.data(),
	};
	vkCmdPipelineBarrier2(frameResource.commandBuffer, &dependencyInfo);

	// Setup attachment info
	VkRenderingAttachmentInfo colorAttachmentInfo{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = swapchainImageViews[swapchainImageIndex],
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,	 // clear the image to start
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE, // keep image for presentation
		.clearValue{.color{{0.01f, 0.01f, 0.01f, 1.0f}}},
	};
	VkRenderingAttachmentInfo depthAttachmentInfo{
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = depthImageView,
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,		 // clear the depth buffer to start
		.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, // don't care after rendering
		.clearValue{.depthStencil{1.0f, 0}},
	};

	// Setup rendering info
	VkRenderingInfo renderingInfo{
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea{.offset{0, 0}, .extent{swapchainWidth, swapchainHeight}},
		.layerCount = 1,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachmentInfo,
		.pDepthAttachment = &depthAttachmentInfo,
	};

	// Begin dynamic rendering
	vkCmdBeginRendering(frameResource.commandBuffer, &renderingInfo);
	{
		// Set the viewport dynamically
		VkViewport viewport{
			.x = 0.0f,
			.y = 0.0f,
			.width = static_cast<float>(swapchainWidth),
			.height = static_cast<float>(swapchainHeight),
		};
		vkCmdSetViewport(frameResource.commandBuffer, 0, 1, &viewport);

		// Set the scissor dynamically
		VkRect2D scissor{
			.offset{0, 0},
			.extent{swapchainWidth, swapchainHeight},
		};
		vkCmdSetScissor(frameResource.commandBuffer, 0, 1, &scissor);

		// Bind the graphics pipeline
		vkCmdBindPipeline(frameResource.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

		// Draw our first triangle!
		vkCmdDraw(frameResource.commandBuffer, 3, 1, 0, 0);
	}
	// End dynamic rendering
	vkCmdEndRendering(frameResource.commandBuffer);

	// Transition the color attachment to presentation layout
	VkImageMemoryBarrier2 presentationBarrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_NONE,
		.dstAccessMask = 0,
		.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		.image = swapchainImages[swapchainImageIndex],
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1
		},
	};
	VkDependencyInfo presentationDependencyInfo{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &presentationBarrier,
	};
	vkCmdPipelineBarrier2(frameResource.commandBuffer, &presentationDependencyInfo);

	// Finish recording commands
	if (vkEndCommandBuffer(frameResource.commandBuffer) != VK_SUCCESS)
	{
		throw RenderError("Failed to record command buffer.");
	}

	// Ensure swapchain image is ready for rendering by waiting on the image-acquired semaphore
	VkSemaphoreSubmitInfo imageAcquireWaitInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = imageAcquiredSemaphore,
		.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
	};

	// Signal that the image is ready for presentation
	std::vector<VkSemaphoreSubmitInfo> semaphoreSignalInfos{
		// Signal the render-complete binary semaphore for this swapchain image
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = renderCompleteSemaphores[swapchainImageIndex],
			.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
		},
		// Signal the timeline semaphore
		{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = timelineSemaphore,
			.value = signalValue,
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		}
	};

	// Submit the command buffer to the graphics queue
	VkCommandBufferSubmitInfo commandBufferSubmitInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = frameResource.commandBuffer,
	};
	VkSubmitInfo2 submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.waitSemaphoreInfoCount = 1,
		.pWaitSemaphoreInfos = &imageAcquireWaitInfo,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &commandBufferSubmitInfo,
		.signalSemaphoreInfoCount = static_cast<uint32_t>(semaphoreSignalInfos.size()),
		.pSignalSemaphoreInfos = semaphoreSignalInfos.data(),
	};
	if (vkQueueSubmit2(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
	{
		throw RenderError("Failed to submit command buffer.");
	}

	// Present the swapchain image!
	VkPresentInfoKHR presentInfo{
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &renderCompleteSemaphores[swapchainImageIndex],
		.swapchainCount = 1,
		.pSwapchains = &swapchain,
		.pImageIndices = &swapchainImageIndex,
		.pResults = nullptr,
	};
	const VkResult presentResult = vkQueuePresentKHR(graphicsQueue, &presentInfo);

	// Handle swapchain recreation if needed
	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
	{
		requireSwapchainRecreation = true;
	}
	else if (presentResult != VK_SUCCESS) { throw RenderError("Failed to present swapchain image."); }
}

void Renderer::shutdown()
{
	// Flush GPU first (if device exists)
	if (device) { vkDeviceWaitIdle(device); }

	for (auto& image : images)
	{
		vkDestroyImageView(device, image.imageView, nullptr);
		vkDestroyImage(device, image.image, nullptr);
		vmaFreeMemory(vmaAllocator, image.allocation);
	}
	images.clear();

	for (VkSampler sampler : samplers)
	{
		vkDestroySampler(device, sampler, nullptr);
	}
	samplers.clear();

	for (auto& buffer : buffers)
	{
		vkDestroyBuffer(device, buffer.buffer, nullptr);
		vmaFreeMemory(vmaAllocator, buffer.allocation);
	}
	buffers.clear();

	// Frame/sync resources
	if (timelineSemaphore)
	{
		vkDestroySemaphore(device, timelineSemaphore, nullptr);
		timelineSemaphore = VK_NULL_HANDLE;
	}

	if (transientCommandPool)
	{
		vkDestroyCommandPool(device, transientCommandPool, nullptr);
		transientCommandPool = VK_NULL_HANDLE;
	}

	for (FrameResources& frameResource : frameResources)
	{
		if (frameResource.imageAcquiredSemaphore)
		{
			vkDestroySemaphore(device, frameResource.imageAcquiredSemaphore, nullptr);
			frameResource.imageAcquiredSemaphore = VK_NULL_HANDLE;
		}
		// Destroying command pool implicity frees command buffers allocated from it
		if (frameResource.commandPool)
		{
			vkDestroyCommandPool(device, frameResource.commandPool, nullptr);
			frameResource.commandPool = VK_NULL_HANDLE;
			frameResource.commandBuffer = VK_NULL_HANDLE;
		}
	}

	// Pipeline
	if (pipelineLayout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		pipelineLayout = VK_NULL_HANDLE;
	}
	if (pipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(device, pipeline, nullptr);
		pipeline = VK_NULL_HANDLE;
	}

	// Shaders
	if (vertShader != VK_NULL_HANDLE)
	{
		vkDestroyShaderModule(device, vertShader, nullptr);
		vertShader = VK_NULL_HANDLE;
	}
	if (fragShader != VK_NULL_HANDLE)
	{
		vkDestroyShaderModule(device, fragShader, nullptr);
		fragShader = VK_NULL_HANDLE;
	}

	destroySwapchain();

	if (vmaAllocator)
	{
		vmaDestroyAllocator(vmaAllocator);
		vmaAllocator = nullptr;
	}

	if (surface)
	{
		vkDestroySurfaceKHR(vulkanInstance, surface, nullptr);
		surface = VK_NULL_HANDLE;
	}

	if (device)
	{
		vkDestroyDevice(device, nullptr);
		device = VK_NULL_HANDLE;
	}

	if (debugMessenger)
	{
		vkDestroyDebugUtilsMessengerEXT(vulkanInstance, debugMessenger, nullptr);
		debugMessenger = VK_NULL_HANDLE;
	}

	if (vulkanInstance)
	{
		vkDestroyInstance(vulkanInstance, nullptr);
		vulkanInstance = VK_NULL_HANDLE;
	}
	volkFinalize();
}

void Renderer::invalidateSwapchain()
{ requireSwapchainRecreation = true; }

// Debug callback for Vulkan validation layers
VKAPI_ATTR VkBool32 VKAPI_CALL Renderer::debugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData
)
{
	if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		std::cerr << "Vulkan validation layer: " << pCallbackData->pMessage << std::endl;
	}

	return VK_FALSE;
}

void Renderer::createVulkanInstance()
{
	VkApplicationInfo applicationInfo{
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pNext = nullptr,
		.pApplicationName = "Vulkan Practice",
		.apiVersion = VulkanAPIVersion,
	};

	// Extensions
	uint32_t instanceExtensionCount = 0;
	const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&instanceExtensionCount);
	std::vector<const char*> requestedExtensions{
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
	};
	for (uint32_t i = 0; i < instanceExtensionCount; ++i)
	{
		requestedExtensions.push_back(extensions[i]);
	}

	// Layers
	std::vector<const char*> requestedLayers{
		"VK_LAYER_KHRONOS_validation",
	};

	VkDebugUtilsMessengerCreateInfoEXT debugInfo{
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
						   | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = debugCallback
	};

	VkInstanceCreateInfo instanceCreateInfo{
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pNext = &debugInfo,
		.pApplicationInfo = &applicationInfo,
		.enabledLayerCount = static_cast<uint32_t>(requestedLayers.size()),
		.ppEnabledLayerNames = requestedLayers.data(),
		.enabledExtensionCount = static_cast<uint32_t>(requestedExtensions.size()),
		.ppEnabledExtensionNames = requestedExtensions.data(),
	};

	if (vkCreateInstance(&instanceCreateInfo, nullptr, &vulkanInstance) != VK_SUCCESS)
	{
		throw RenderError("Failed to create Vulkan instance.");
	}

	volkLoadInstance(vulkanInstance);

	// Create debug messenger
	if (vkCreateDebugUtilsMessengerEXT(vulkanInstance, &debugInfo, nullptr, &debugMessenger) != VK_SUCCESS)
	{
		throw RenderError("Failed to create debug messenger.");
	}
}

void Renderer::createSurface()
{
	if (!SDL_Vulkan_CreateSurface(window, vulkanInstance, nullptr, &surface))
	{
		throw RenderError("Vulkan surface creation failed!\n\n" + std::string(SDL_GetError()));
	}
}

// Defaults to first device, but will try to find a discrete GPU if available.
// - Replace with a more sophisticated selection algorithm at some point!
VkPhysicalDevice Renderer::selectPhysicalDevice()
{
	uint32_t physicalDeviceCount = 0;
	vkEnumeratePhysicalDevices(vulkanInstance, &physicalDeviceCount, nullptr);

	std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
	vkEnumeratePhysicalDevices(vulkanInstance, &physicalDeviceCount, physicalDevices.data());

	if (physicalDeviceCount == 0) { throw RenderError("No physical devices found."); }

	// Default to first device
	VkPhysicalDevice selectedDevice = physicalDevices[0];

	// Try to find a discrete GPU
	for (const auto& currentDevice : physicalDevices)
	{
		VkPhysicalDeviceProperties deviceProperties{};
		vkGetPhysicalDeviceProperties(currentDevice, &deviceProperties);

		if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			selectedDevice = currentDevice;
			break;
		}
	}

	// Print selected device name
	VkPhysicalDeviceProperties props{};
	vkGetPhysicalDeviceProperties(selectedDevice, &props);
	std::cout << "Selected physical device: " << props.deviceName << std::endl;

	// Ensure the requested swapchain format is supported
	uint32_t formatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(selectedDevice, surface, &formatCount, nullptr);

	std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(selectedDevice, surface, &formatCount, surfaceFormats.data());

	bool formatSupported = false;
	for (const VkSurfaceFormatKHR& surfaceFormat : surfaceFormats)
	{
		if (surfaceFormat.format == swapchainFormat)
		{
			formatSupported = true;
			break;
		}
	}

	if (!formatSupported)
	{
		throw RenderError(
			"Requested swapchain format not supported by the selected physical device and surface combination."
		);
	}

	return selectedDevice;
}

void Renderer::selectGraphicsQueue()
{
	// Get queue family count
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueFamilyCount, nullptr);

	// Get queue family properties
	std::vector<VkQueueFamilyProperties2> queueFamilies(queueFamilyCount, {VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2});
	vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &queueFamilyCount, queueFamilies.data());

	// Select one that supports both graphics and presentation
	for (uint32_t i = 0; i < queueFamilyCount; ++i)
	{
		VkBool32 presentSupport = VK_FALSE;
		vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);

		const VkQueueFamilyProperties2& queueFamily = queueFamilies[i];
		if (presentSupport == VK_TRUE && queueFamily.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			graphicsQueueFamilyIndex = i;
			return;
		}
	}

	throw RenderError("No suitable graphics queue found.");
}

void Renderer::createDevice()
{
	// Get supported features
	VkPhysicalDeviceVulkan14Features supportedFeatures14{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, .pNext = nullptr
	};
	VkPhysicalDeviceVulkan13Features supportedFeatures13{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &supportedFeatures14
	};
	VkPhysicalDeviceVulkan12Features supportedFeatures12{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &supportedFeatures13
	};
	VkPhysicalDeviceFeatures2 supportedFeatures{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &supportedFeatures12
	};
	vkGetPhysicalDeviceFeatures2(physicalDevice, &supportedFeatures);

	// Check for required features
	if (!supportedFeatures13.dynamicRendering || !supportedFeatures13.synchronization2
		|| !supportedFeatures12.timelineSemaphore || !supportedFeatures12.bufferDeviceAddress
		|| !supportedFeatures12.scalarBlockLayout || !supportedFeatures12.descriptorIndexing
		|| !supportedFeatures12.descriptorBindingSampledImageUpdateAfterBind
		|| !supportedFeatures12.descriptorBindingPartiallyBound || !supportedFeatures12.runtimeDescriptorArray
		|| !supportedFeatures12.shaderSampledImageArrayNonUniformIndexing || !supportedFeatures.features.shaderInt64
		|| !supportedFeatures.features.multiDrawIndirect)
	{
		throw RenderError("Physical device does not support required features.");
	}

	// Enable required features
	VkPhysicalDeviceVulkan14Features enabledFeatures14{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
		.pNext = nullptr,
	};
	VkPhysicalDeviceVulkan13Features enabledFeatures13{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
		.pNext = &enabledFeatures14,
		.synchronization2 = VK_TRUE,
		.dynamicRendering = VK_TRUE,
	};
	VkPhysicalDeviceVulkan12Features enabledFeatures12{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
		.pNext = &enabledFeatures13,
		.descriptorIndexing = VK_TRUE,
		.shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
		.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
		.descriptorBindingPartiallyBound = VK_TRUE,
		.runtimeDescriptorArray = VK_TRUE,
		.scalarBlockLayout = VK_TRUE,
		.timelineSemaphore = VK_TRUE,
		.bufferDeviceAddress = VK_TRUE,
	};
	VkPhysicalDeviceFeatures2 enabledFeatures{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &enabledFeatures12,
		.features{
			.multiDrawIndirect = VK_TRUE,
			.shaderInt64 = VK_TRUE,
		}
	};

	// Device extensions
	const std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	// Request queues
	std::vector<float> queuePriorities{1.0f};
	VkDeviceQueueCreateInfo graphicsQueueInfo{
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = graphicsQueueFamilyIndex,
		.queueCount = 1,
		.pQueuePriorities = queuePriorities.data(),
	};

	// Create logical device
	VkDeviceCreateInfo deviceCreateInfo{
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &enabledFeatures,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &graphicsQueueInfo,
		.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
		.ppEnabledExtensionNames = deviceExtensions.data(),
		.pEnabledFeatures = nullptr,
	};

	if (vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device) != VK_SUCCESS)
	{
		throw RenderError("Failed to create logical device.");
	}

	// Get the graphics queue
	vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
	if (!graphicsQueue) { throw RenderError("Failed to get graphics queue."); }
}

void Renderer::initializeVMA()
{
	VmaVulkanFunctions vmaFunctionInfo{};
	VmaAllocatorCreateInfo vmaAllocatorInfo{
		.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
		.physicalDevice = physicalDevice,
		.device = device,
		.pVulkanFunctions = &vmaFunctionInfo,
		.instance = vulkanInstance,
		.vulkanApiVersion = VulkanAPIVersion
	};

	vmaImportVulkanFunctionsFromVolk(&vmaAllocatorInfo, &vmaFunctionInfo);

	if (vmaCreateAllocator(&vmaAllocatorInfo, &vmaAllocator) != VK_SUCCESS)
	{
		throw RenderError("Failed to create VMA allocator.");
	}
}

void Renderer::createSwapchain()
{
	int width, height;
	if (!SDL_GetWindowSizeInPixels(window, &width, &height)) { throw RenderError("Error getting window size."); }

	// Get surface capabilities
	VkSurfaceCapabilitiesKHR surfaceCapabilities{};
	if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities) != VK_SUCCESS)
	{
		throw RenderError("Failed to get surface capabilities.");
	}

	// Clamp swapchain extent to surface capabilities
	swapchainWidth = std::clamp(
		static_cast<uint32_t>(width), surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width
	);
	swapchainHeight = std::clamp(
		static_cast<uint32_t>(height), surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height
	);

	// Should never actually happen, but just in case
	if (swapchainWidth == 0 || swapchainHeight == 0) { throw RenderError("Invalid swapchain dimensions."); }

	// Determine the number of images in the swapchain
	uint32_t requestedImageCount = std::max(2u, surfaceCapabilities.minImageCount);
	if (surfaceCapabilities.maxImageCount > 0)
	{
		requestedImageCount = std::min(requestedImageCount, surfaceCapabilities.maxImageCount);
	}

	// Create swapchain
	VkSwapchainCreateInfoKHR swapchainCreateInfo{
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surface,
		.minImageCount = requestedImageCount,
		.imageFormat = swapchainFormat,
		.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
		.imageExtent{.width = swapchainWidth, .height = swapchainHeight},
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.preTransform = surfaceCapabilities.currentTransform,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR
	};

	if (vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain) != VK_SUCCESS)
	{
		throw RenderError("Failed to create swapchain.");
	}

	// Get the swapchain images
	uint32_t swapchainImageCount = 0;
	vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, nullptr);
	swapchainImages.resize(swapchainImageCount);
	vkGetSwapchainImagesKHR(device, swapchain, &swapchainImageCount, swapchainImages.data());
	swapchainImageViews.resize(swapchainImageCount);

	// Create image view into each swapchain image
	for (size_t i = 0; i < swapchainImages.size(); ++i)
	{
		VkImageViewCreateInfo imageViewInfo{
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = swapchainImages[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = swapchainFormat,
			.subresourceRange{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		if (vkCreateImageView(device, &imageViewInfo, nullptr, &swapchainImageViews[i]) != VK_SUCCESS)
		{
			throw RenderError(std::format("Failed to create image view for swapchain image {}.", i));
		}
	}

	// Create semaphore for each swapchain image
	renderCompleteSemaphores.resize(swapchainImages.size());
	for (size_t i = 0; i < renderCompleteSemaphores.size(); ++i)
	{
		VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

		if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderCompleteSemaphores[i]) != VK_SUCCESS)
		{
			throw RenderError(std::format("Failed to create render-complete semaphore for swapchain image {}.", i));
		}
	}

	// Create swapchain depth image
	VkImageCreateInfo depthImageInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = depthFormat,
		.extent{.width = swapchainWidth, .height = swapchainHeight, .depth = 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

	VmaAllocationCreateInfo depthAllocationInfo{
		.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	if (vmaCreateImage(vmaAllocator, &depthImageInfo, &depthAllocationInfo, &depthImage, &depthImageAllocation, nullptr)
		!= VK_SUCCESS)
	{
		throw RenderError("Failed to create depth image.");
	}

	// Create image view for depth image
	VkImageViewCreateInfo depthImageViewInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = depthImage,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = depthFormat,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1
		}
	};

	if (vkCreateImageView(device, &depthImageViewInfo, nullptr, &depthImageView) != VK_SUCCESS)
	{
		throw RenderError("Failed to create image view for depth image.");
	}
}

void Renderer::destroySwapchain()
{
	for (VkImageView imageView : swapchainImageViews)
	{
		if (imageView != VK_NULL_HANDLE) { vkDestroyImageView(device, imageView, nullptr); }
	}
	swapchainImageViews.clear();

	for (VkSemaphore& semaphore : renderCompleteSemaphores)
	{
		if (semaphore != VK_NULL_HANDLE) { vkDestroySemaphore(device, semaphore, nullptr); }
	}
	renderCompleteSemaphores.clear();
	swapchainImages.clear();

	if (swapchain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(device, swapchain, nullptr);
		swapchain = VK_NULL_HANDLE;
	}

	if (depthImageView != VK_NULL_HANDLE)
	{
		vkDestroyImageView(device, depthImageView, nullptr);
		depthImageView = VK_NULL_HANDLE;
	}
	if (depthImage != VK_NULL_HANDLE)
	{
		vmaDestroyImage(vmaAllocator, depthImage, depthImageAllocation);
		depthImage = VK_NULL_HANDLE;
	}
}

// Create a shader module from a GLSL shader file using shaderc
VkShaderModule Renderer::createShaderModule(const std::string& filename, shaderc_shader_kind kind) const
{
	// Read shader source from file
	std::string shaderPath = "shaders/" + filename;
	std::string shaderSource = readTextFile(shaderPath);

	if (shaderSource.empty()) { throw RenderError("Failed to read shader source for \"" + filename + "\"."); }

	// Compile shader source to SPIR-V using shaderc
	std::cout << "Compiling shader: " << shaderPath << std::endl;

	shaderc::Compiler compiler;
	shaderc::CompileOptions options;

	options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_4);
	options.SetTargetSpirv(shaderc_spirv_version_1_6);
	options.SetOptimizationLevel(shaderc_optimization_level_performance);

	shaderc::CompilationResult result = compiler.CompileGlslToSpv(shaderSource, kind, filename.c_str(), options);

	if (result.GetCompilationStatus() != shaderc_compilation_status_success)
	{
		std::cerr << "Shader compilation failed for \"" << filename << "\": " << result.GetErrorMessage() << std::endl;
		throw RenderError("Failed to compile shader: " + filename + ".\n\n" + result.GetErrorMessage());
	}

	// Create shader module from SPIR-V
	const size_t spirvSize = (result.cend() - result.cbegin()) * sizeof(uint32_t);

	VkShaderModuleCreateInfo shaderModuleInfo{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = spirvSize,
		.pCode = result.cbegin(),
	};

	VkShaderModule shaderModule = VK_NULL_HANDLE;
	if (vkCreateShaderModule(device, &shaderModuleInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		throw RenderError("Failed to create shader module for \"" + filename + "\".");
	}

	return shaderModule;
}

void Renderer::createShaders()
{
	// Vertex shader
	vertShader = createShaderModule("shader.vert", shaderc_vertex_shader);

	// Fragment shader
	fragShader = createShaderModule("shader.frag", shaderc_fragment_shader);
}

VkPipeline Renderer::createGraphicsPipeline()
{
	// Create pipeline layout
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 0,
		.pushConstantRangeCount = 0,
	};

	if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
	{
		throw RenderError("Failed to create pipeline layout.");
	}

	// Shader stages
	const char* entryPoint = "main";
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages{
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vertShader,
			.pName = entryPoint,
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = fragShader,
			.pName = entryPoint,
		}
	};

	// Vertex input state (vertex pulling)
	VkPipelineVertexInputStateCreateInfo vertexInputInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	};

	// Input assembly state
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};

	// Depth stencil state
	VkPipelineDepthStencilStateCreateInfo depthStencilInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
		.stencilTestEnable = VK_FALSE,
	};

	// Viewport & scissor state (dynamic)
	VkPipelineViewportStateCreateInfo viewportInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = nullptr,
		.scissorCount = 1,
		.pScissors = nullptr,
	};

	// Rasterization state
	VkPipelineRasterizationStateCreateInfo rasterizationInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};

	// Multisample state (none)
	VkPipelineMultisampleStateCreateInfo multisampleInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	// Color blend state (alpha-blending disabled)
	VkPipelineColorBlendAttachmentState colorBlendAttachment{
		.blendEnable = VK_FALSE,
		.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};
	VkPipelineColorBlendStateCreateInfo colorBlendInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &colorBlendAttachment,
	};

	// Dynamic state (viewport & scissor)
	std::vector<VkDynamicState> dynamicStates{
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};
	VkPipelineDynamicStateCreateInfo dynamicStateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
		.pDynamicStates = dynamicStates.data(),
	};

	// Dynamic rendering info
	VkPipelineRenderingCreateInfo renderingInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = 1,
		.pColorAttachmentFormats = &swapchainFormat,
		.depthAttachmentFormat = depthFormat,
	};

	// Finally, create the graphics pipeline
	VkGraphicsPipelineCreateInfo pipelineInfo{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &renderingInfo,
		.stageCount = static_cast<uint32_t>(shaderStages.size()),
		.pStages = shaderStages.data(),
		.pVertexInputState = &vertexInputInfo,
		.pInputAssemblyState = &inputAssemblyInfo,
		.pViewportState = &viewportInfo,
		.pRasterizationState = &rasterizationInfo,
		.pMultisampleState = &multisampleInfo,
		.pDepthStencilState = &depthStencilInfo,
		.pColorBlendState = &colorBlendInfo,
		.pDynamicState = &dynamicStateInfo,
		.layout = pipelineLayout,
		.renderPass = VK_NULL_HANDLE,
	};

	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
	{
		throw RenderError("Failed to create graphics pipeline.");
	}

	return pipeline;
}

void Renderer::createSyncResources()
{
	// Create timeline semaphore
	VkSemaphoreTypeCreateInfo timelineSemaphoreInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		.initialValue = MaxFramesInFlight,
	};
	VkSemaphoreCreateInfo semaphoreInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = &timelineSemaphoreInfo,
	};
	if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &timelineSemaphore) != VK_SUCCESS)
	{
		throw RenderError("Failed to create timeline semaphore.");
	}

	// Per-frame binary semaphore for image acquisition
	for (FrameResources& frameResource : frameResources)
	{
		VkSemaphoreCreateInfo imageAcquiredSemaphoreInfo{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		};
		if (vkCreateSemaphore(device, &imageAcquiredSemaphoreInfo, nullptr, &frameResource.imageAcquiredSemaphore)
			!= VK_SUCCESS)
		{
			throw RenderError("Failed to create per-frame image-acquired semaphore.");
		}
	}
}

void Renderer::recreateImageAcquiredSemaphore(FrameResources& frameResource)
{
	// Destroy existing semaphore
	vkDestroySemaphore(device, frameResource.imageAcquiredSemaphore, nullptr);
	frameResource.imageAcquiredSemaphore = VK_NULL_HANDLE;

	// Create a new one
	VkSemaphoreCreateInfo semaphoreInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};
	if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frameResource.imageAcquiredSemaphore) != VK_SUCCESS)
	{
		throw RenderError("Failed to recreate image-acquired semaphore.");
	}
}

void Renderer::createCommandBuffers()
{
	// Transient command pool
	VkCommandPoolCreateInfo transientPoolInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = graphicsQueueFamilyIndex,
	};
	if (vkCreateCommandPool(device, &transientPoolInfo, nullptr, &transientCommandPool) != VK_SUCCESS)
	{
		throw RenderError("Failed to create transient command pool.");
	}

	// Per-frame command pools/buffers
	for (FrameResources& frameResource : frameResources)
	{
		// Create per-frame command pool
		VkCommandPoolCreateInfo commandPoolInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.queueFamilyIndex = graphicsQueueFamilyIndex,
		};
		if (vkCreateCommandPool(device, &commandPoolInfo, nullptr, &frameResource.commandPool) != VK_SUCCESS)
		{
			throw RenderError("Failed to create per-frame command pool.");
		}

		// Create per-frame command buffer
		VkCommandBufferAllocateInfo commandBufferInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = frameResource.commandPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};
		if (vkAllocateCommandBuffers(device, &commandBufferInfo, &frameResource.commandBuffer) != VK_SUCCESS)
		{
			throw RenderError("Failed to allocate per-frame command buffer.");
		}
	}
}

VkCommandBuffer Renderer::startTransientCommandBuffer()
{
	// Allocate
	VkCommandBufferAllocateInfo allocationInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = transientCommandPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	if (vkAllocateCommandBuffers(device, &allocationInfo, &commandBuffer) != VK_SUCCESS)
	{
		throw RenderError("Failed to allocate transient command buffer.");
	}

	// Begin
	VkCommandBufferBeginInfo beginInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
	{
		vkFreeCommandBuffers(device, transientCommandPool, 1, &commandBuffer);
		throw RenderError("Failed to begin transient command buffer.");
	}

	return commandBuffer;
}

void Renderer::submitTransientCommandBuffer(VkCommandBuffer commandBuffer)
{
	// Finish recording commands
	if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) { throw RenderError("Failed to record transient command buffer."); }

	// Submit the command buffer to the graphics queue (transfer queue eventually?)
	VkCommandBufferSubmitInfo commandBufferSubmitInfo{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = commandBuffer,
	};
	VkSubmitInfo2 submitInfo{
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &commandBufferSubmitInfo,
	};
	if (vkQueueSubmit2(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
	{
		throw RenderError("Failed to submit transient command buffer.");
	}
	// Wait and clean up buffer (use better sync method eventually)
	vkQueueWaitIdle(graphicsQueue);
	vkFreeCommandBuffers(device, transientCommandPool, 1, &commandBuffer);
}

std::pair<uint32_t, GPUBuffer>
Renderer::createImage(VkCommandBuffer commandBuffer, unsigned char* imageData, uint32_t width, uint32_t height, int channels)
{
	VkFormat imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
	VmaAllocationCreateInfo allocationInfo{.usage = VMA_MEMORY_USAGE_AUTO};
	GPUImage gpuImage;

	// Create image
	VkImageCreateInfo imageInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = imageFormat,
		.extent{width, height, 1},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	if (vmaCreateImage(vmaAllocator, &imageInfo, &allocationInfo, &gpuImage.image, &gpuImage.allocation, nullptr)
		!= VK_SUCCESS)
	{
		throw RenderError("Failed to create image.");
	}

	// Create image view
	VkImageViewCreateInfo imageViewInfo{
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = gpuImage.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = imageFormat,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1
		}
	};
	if (vkCreateImageView(device, &imageViewInfo, nullptr, &gpuImage.imageView) != VK_SUCCESS)
	{
		throw RenderError("Failed to create image view.");
	}

	// Transition image to transfer-DST
	VkImageMemoryBarrier2 transferBarrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_NONE,
		.srcAccessMask = VK_ACCESS_2_NONE,
		.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
		.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.image = gpuImage.image,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1
		},
	};
	VkDependencyInfo transferDependencyInfo{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &transferBarrier,
	};
	vkCmdPipelineBarrier2(commandBuffer, &transferDependencyInfo);

	// Create staging buffer and issue record copy operation
	const size_t byteSize = width * height * channels;
	GPUBuffer stagingBuffer =
		createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, byteSize, true, VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
	mapCopyBufferData(stagingBuffer, 0, imageData, byteSize);

	// Record command to make final copy to image in GPU memory
	VkBufferImageCopy bufferImageCopy{
		.imageSubresource =
			{
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = 0,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		.imageExtent = {width, height, 1},
	};
	vkCmdCopyBufferToImage(
		commandBuffer, stagingBuffer.buffer, gpuImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferImageCopy
	);

	// Transition image for shader read/sampling
	VkImageMemoryBarrier2 shaderReadBarrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
		.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.image = gpuImage.image,
		.subresourceRange{
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1
		},
	};
	VkDependencyInfo shaderReadDependencyInfo{
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &shaderReadBarrier,
	};
	vkCmdPipelineBarrier2(commandBuffer, &shaderReadDependencyInfo);

	images.push_back(gpuImage);

	// Image ID is 1-based (0 is NULL, ID - 1 is index)
	const uint32_t imageID = static_cast<uint32_t>(images.size());
	return {imageID, stagingBuffer};
}

GPUBuffer Renderer::createBuffer(VkBufferUsageFlags usage, size_t byteSize, bool mappable, VmaMemoryUsage memoryUsage)
{
	GPUBuffer gpuBuffer;

	// Create buffer and vma allocation
	VkBufferCreateInfo bufferInfo{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = byteSize,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	VmaAllocationCreateInfo allocationInfo{
		.flags = mappable ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT : 0u,
		.usage = memoryUsage,
	};
	if (vmaCreateBuffer(vmaAllocator, &bufferInfo, &allocationInfo, &gpuBuffer.buffer, &gpuBuffer.allocation, nullptr)
		!= VK_SUCCESS)
	{
		throw RenderError("Failed to create buffer.");
	}

	// BDA send device pointer
	if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
	{
		VkBufferDeviceAddressInfo bdaInfo{
			.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
			.buffer = gpuBuffer.buffer,
		};
		gpuBuffer.deviceAddress = vkGetBufferDeviceAddress(device, &bdaInfo);
	}

	return gpuBuffer;
}

void Renderer::mapCopyBufferData(const GPUBuffer& buffer, size_t bufferOffset, void* data, size_t byteSize)
{
	void* bufferPtr = nullptr;
	if (vmaMapMemory(vmaAllocator, buffer.allocation, &bufferPtr) != VK_SUCCESS)
	{
		throw RenderError("Failed to map buffer memory.");
	}

	// Copy into the mapped range at given offset
	std::memcpy(static_cast<char*>(bufferPtr) + bufferOffset, data, byteSize);

	vmaUnmapMemory(vmaAllocator, buffer.allocation);
}

void Renderer::createFallbackTexture()
{
	// Fallback image
	uint32_t whitePixelData = 0xFFFFFFFF;
	Image whitePixel{
		.width = 1,
		.height = 1,
		.channels = 4,
		.data = reinterpret_cast<unsigned char*>(&whitePixelData),
	};

	VkCommandBuffer fallbackImageCommandBuffer = startTransientCommandBuffer();
	auto [whitePixelID, whitePixelStagingBuffer] =
		createImage(fallbackImageCommandBuffer, whitePixel.data, whitePixel.width, whitePixel.height, whitePixel.channels);
	fallbackImageID = whitePixelID;
	submitTransientCommandBuffer(fallbackImageCommandBuffer);
	vmaDestroyBuffer(vmaAllocator, whitePixelStagingBuffer.buffer, whitePixelStagingBuffer.allocation);

	// Fallback texture sampler
	VkSamplerCreateInfo samplerInfo{
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_NEAREST,
		.minFilter = VK_FILTER_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
		.compareEnable = VK_FALSE,
	};
	VkSampler sampler = VK_NULL_HANDLE;
	if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
	{
		throw RenderError("Failed to create texture sampler.");
	}

	// Store sampler, get ID, store texture
	samplers.push_back(sampler);
	uint32_t fallbackSamplerID = static_cast<uint32_t>(samplers.size());
	textures.push_back(Texture{.imageID = fallbackImageID, .samplerID = fallbackSamplerID});
}

void Renderer::loadGLTF(const std::string& filepath)
{
	if (!std::filesystem::exists(filepath)) { throw RenderError("GLTF file does not exists!"); }
	std::cout << std::format("Loading GLTF: {}", filepath) << std::endl;

	// Load and parse GLTF
	tg3_model model;
	tg3_parse_options modelOptions;
	tg3_error_stack modelErrors;

	tg3_parse_options_init(&modelOptions);
	tg3_error_stack_init(&modelErrors);
	tg3_error_code parseResult =
		tg3_parse_file(&model, &modelErrors, filepath.c_str(), static_cast<uint32_t>(filepath.size()), &modelOptions);

	// Handle parse errors
	if (parseResult != TG3_OK)
	{
		std::cerr << "GLTF parsing failed, errors found:" << std::endl;
		for (uint32_t i = 0; i < modelErrors.count; ++i)
		{
			std::cerr << modelErrors.entries[i].message << std::endl;
		}
		tg3_error_stack_free(&modelErrors);
		throw RenderError("GLTF parsing failed!");
	}
	tg3_error_stack_free(&modelErrors);

	// Load images
	std::filesystem::path modelDirectory = std::filesystem::path(filepath).parent_path();
	std::vector<Image> modelImages = loadImages(model, modelDirectory);
	std::vector<uint32_t> modelImageIDs = uploadImages(modelImages);

	// Free stb image mem after VRAM upload
	for (const Image& image : modelImages)
	{
		stbi_image_free(image.data);
	}

	// Load samplers, textures, mats, and meshes
	std::vector<uint32_t> modelSamplerIDs = loadSamplers(model);
	std::vector<uint32_t> modelTextureIDs = loadTextures(model, modelImageIDs, modelSamplerIDs);
	std::vector<uint32_t> modelMaterialIDs = loadMaterials(model, modelTextureIDs);
	std::vector<uint32_t> modelMeshIDs = loadMeshes(model, modelMaterialIDs);

	// Import scene
	const tg3_scene* tg3Scene = &model.scenes[model.default_scene != -1 ? model.default_scene : 0];

	// Iterate root nodes, importing each node and its children
	for (uint32_t i = 0; i < tg3Scene->nodes_count; ++i)
	{
		uint32_t nodeID = importNode(model, tg3Scene->nodes[i], 0, lastRootNodeID, modelMeshIDs);

		// First root node
		if (!rootNodeID)
		{
			rootNodeID = nodeID;
			lastRootNodeID = nodeID;
		}
		else
		{
			lastRootNodeID = nodeID;
		}
	}

	std::cout << std::format("Loaded {} nodes.", scene.size()) << std::endl;

	// Cleanup
	tg3_model_free(&model);
	std::cout << "GLTF loaded successfully!" << std::endl;
}

std::vector<Image> Renderer::loadImages(const tg3_model& model, const std::filesystem::path& imageDir)
{
	std::vector<Image> loadedImages(model.images_count);

	for (uint32_t i = 0; i < model.images_count; ++i)
	{
		Image& image = loadedImages[i];
		std::filesystem::path imagePath = imageDir / model.images[i].uri.data;

		std::cout << std::format("Loading image {}/{}: {}", i + 1, model.images_count, model.images[i].uri.data)
				  << std::endl;

		image.data = stbi_load(imagePath.string().c_str(), &image.width, &image.height, &image.channels, 4);
		if (!image.data) { throw RenderError("Failed to load image: " + imagePath.string()); }
	}

	return loadedImages;
}

std::vector<uint32_t> Renderer::uploadImages(const std::vector<Image>& cpuImages)
{
	VkCommandBuffer commandBuffer = startTransientCommandBuffer();

	std::vector<GPUBuffer> stagingBuffers;
	stagingBuffers.reserve(cpuImages.size());

	std::vector<uint32_t> imageIDs(cpuImages.size(), fallbackImageID);

	// Upload images to GPU textures
	for (uint32_t i = 0; i < cpuImages.size(); ++i)
	{
		const Image& image = cpuImages[i];

		if (image.data)
		{
			auto [imageID, imageStagingBuffer] = createImage(commandBuffer, image.data, image.width, image.height, 4);

			imageIDs[i] = imageID;
			stagingBuffers.push_back(imageStagingBuffer);
		}
	}

	submitTransientCommandBuffer(commandBuffer);

	// Cleanup staging buffers
	for (const GPUBuffer& buffer : stagingBuffers)
	{
		vmaDestroyBuffer(vmaAllocator, buffer.buffer, buffer.allocation);
	}

	std::cout << std::format("Uploaded {} images.", imageIDs.size()) << std::endl;
	return imageIDs;
}

std::vector<uint32_t> Renderer::loadSamplers(const tg3_model& model)
{
	std::vector<uint32_t> samplerIDs(model.samplers_count);

	for (uint32_t i = 0; i < model.samplers_count; ++i)
	{
		const tg3_sampler& tg3Sampler = model.samplers[i];

		static const std::unordered_map<int32_t, std::tuple<VkFilter, VkSamplerMipmapMode, float>> filterMap{
			{TG3_TEXTURE_FILTER_NEAREST, {VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.25f}},
			{TG3_TEXTURE_FILTER_LINEAR, {VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, 0.25f}},
			{TG3_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST,
			 {VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_LOD_CLAMP_NONE}},
			{TG3_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR,
			 {VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_LOD_CLAMP_NONE}},
			{TG3_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST,
			 {VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_LOD_CLAMP_NONE}},
			{TG3_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR, {VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_LOD_CLAMP_NONE}},
		};

		static const std::unordered_map<int32_t, VkSamplerAddressMode> wrapMap{
			{TG3_TEXTURE_WRAP_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT},
			{TG3_TEXTURE_WRAP_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE},
			{TG3_TEXTURE_WRAP_MIRRORED_REPEAT, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT},
		};

		VkSamplerCreateInfo samplerInfo{
			.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
			.magFilter = (tg3Sampler.mag_filter == -1) ? VK_FILTER_LINEAR : std::get<0>(filterMap.at(tg3Sampler.mag_filter)),
			.minFilter = (tg3Sampler.min_filter == -1) ? VK_FILTER_LINEAR : std::get<0>(filterMap.at(tg3Sampler.min_filter)),
			.mipmapMode = (tg3Sampler.min_filter == -1) ? VK_SAMPLER_MIPMAP_MODE_LINEAR
														: std::get<1>(filterMap.at(tg3Sampler.min_filter)),
			.addressModeU = (tg3Sampler.wrap_s == -1) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : wrapMap.at(tg3Sampler.wrap_s),
			.addressModeV = (tg3Sampler.wrap_t == -1) ? VK_SAMPLER_ADDRESS_MODE_REPEAT : wrapMap.at(tg3Sampler.wrap_t),
			.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
			.compareEnable = VK_FALSE,
			.minLod = 0.0f,
			.maxLod = (tg3Sampler.min_filter == -1) ? VK_LOD_CLAMP_NONE : std::get<2>(filterMap.at(tg3Sampler.min_filter)),
		};

		VkSampler sampler = VK_NULL_HANDLE;
		if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
		{
			std::cerr << "Failed to create texture sampler." << std::endl;
			samplerIDs[i] = textures[0].samplerID;
		}
		else
		{
			samplers.push_back(sampler);
			samplerIDs[i] = static_cast<uint32_t>(samplers.size());
		}
	}

	std::cout << std::format("Loaded {} samplers.", samplerIDs.size()) << std::endl;
	return samplerIDs;
}

std::vector<uint32_t> Renderer::loadTextures(
	const tg3_model& model, const std::vector<uint32_t>& imageIDs, const std::vector<uint32_t>& samplerIDs
)
{
	assert(textures.size() + model.textures_count <= MaxTextures && "Exceeded max texture count!");

	std::vector<uint32_t> textureIDs(model.textures_count);
	for (uint32_t i = 0; i < model.textures_count; ++i)
	{
		const tg3_texture& tg3Texture = model.textures[i];
		textures.push_back(
			Texture{
				.imageID = imageIDs[tg3Texture.source],
				.samplerID = tg3Texture.sampler == -1 ? textures[0].samplerID : samplerIDs[tg3Texture.sampler],
			}
		);
		textureIDs[i] = static_cast<uint32_t>(textures.size());
	}

	std::cout << std::format("Loaded {} textures.", textureIDs.size()) << std::endl;
	return textureIDs;
}

std::vector<uint32_t> Renderer::loadMaterials(const tg3_model& model, const std::vector<uint32_t>& textureIDs)
{
	std::vector<uint32_t> materialIDs(model.materials_count);
	for (uint32_t i = 0; i < model.materials_count; ++i)
	{
		const tg3_material& tg3Material = model.materials[i];
		materials.push_back(
			Material{
				.baseColor = glm::vec4(
					tg3Material.pbr_metallic_roughness.base_color_factor[0],
					tg3Material.pbr_metallic_roughness.base_color_factor[1],
					tg3Material.pbr_metallic_roughness.base_color_factor[2],
					tg3Material.pbr_metallic_roughness.base_color_factor[3]
				),
				.textureID = tg3Material.pbr_metallic_roughness.base_color_texture.index != -1
								 ? textureIDs[tg3Material.pbr_metallic_roughness.base_color_texture.index]
								 : 0,
			}
		);
		materialIDs[i] = static_cast<uint32_t>(materials.size());
	}

	std::cout << std::format("Loaded {} materials.", materialIDs.size()) << std::endl;
	return materialIDs;
}

std::vector<uint32_t> Renderer::loadMeshes(const tg3_model& model, const std::vector<uint32_t>& materialIDs)
{
	std::vector<uint32_t> meshIDs(model.meshes_count);

	for (uint32_t i = 0; i < model.meshes_count; ++i)
	{
		Mesh mesh;
		const tg3_mesh* tg3Mesh = &model.meshes[i];

		// Copy name
		mesh.name = tg3Mesh->name.data != nullptr ? tg3Mesh->name.data : "No Name";

		// Attribute data copy lambda
		auto writeAttribute = [this, &model]<typename T>(T Vertex::* member, const tg3_str_int_pair* attr) {
			const tg3_accessor* accessor = &model.accessors[attr->value];
			const tg3_buffer_view* bufferView = &model.buffer_views[accessor->buffer_view];
			const tg3_buffer* buffer = &model.buffers[bufferView->buffer];
			const size_t bufferOffset = bufferView->byte_offset + accessor->byte_offset;
			const size_t stride = bufferView->byte_stride != 0 ? bufferView->byte_stride : sizeof(T);

			for (uint64_t j = 0; j < accessor->count; ++j)
			{
				const size_t elementOffset = bufferOffset + j * stride;
				const float* data = reinterpret_cast<const float*>(buffer->data.data + elementOffset);

				if constexpr (std::is_same<T, glm::vec3>())
				{
					sceneVertices[vertexOffset + j].*member = glm::vec3(data[0], data[1], data[2]);
				}
				else if constexpr (std::is_same<T, glm::vec2>())
				{
					sceneVertices[vertexOffset + j].*member = glm::vec2(data[0], data[1]);
				}
			}
		};

		// Copy vertex data
		mesh.subMeshes.resize(tg3Mesh->primitives_count);
		for (uint32_t j = 0; j < tg3Mesh->primitives_count; ++j)
		{
			const tg3_primitive* primitive = &tg3Mesh->primitives[j];
			mesh.subMeshes[j].materialID = materialIDs[primitive->material];
			mesh.subMeshes[j].vertexStart = vertexOffset;

			for (uint32_t k = 0; k < primitive->attributes_count; ++k)
			{
				const tg3_str_int_pair* attr = &primitive->attributes[k];
				if (strcmp(attr->key.data, "POSITION") == 0)
				{
					const tg3_accessor* accessor = &model.accessors[attr->value];

					assert(accessor->type == TG3_TYPE_VEC3 && accessor->component_type == TG3_COMPONENT_TYPE_FLOAT);
					assert(vertexOffset + accessor->count <= sceneVertices.size() && "Not enough space to load vertices");

					mesh.subMeshes[j].vertexCount = accessor->count;
					writeAttribute(&Vertex::position, attr);
				}
				else if (strcmp(attr->key.data, "NORMAL") == 0)
				{
					const tg3_accessor* accessor = &model.accessors[attr->value];
					assert(accessor->type == TG3_TYPE_VEC3 && accessor->component_type == TG3_COMPONENT_TYPE_FLOAT);
					writeAttribute(&Vertex::normal, attr);
				}
				else if (strcmp(attr->key.data, "COLOR_0") == 0)
				{
					const tg3_accessor* accessor = &model.accessors[attr->value];

					assert(accessor->type == TG3_TYPE_VEC3 || accessor->type == TG3_TYPE_VEC4);
					assert(accessor->component_type == TG3_COMPONENT_TYPE_FLOAT);

					writeAttribute(&Vertex::color, attr);
				}
				else if (strcmp(attr->key.data, "TEXCOORD_0") == 0)
				{
					const tg3_accessor* accessor = &model.accessors[attr->value];

					assert(accessor->type == TG3_TYPE_VEC2 && accessor->component_type == TG3_COMPONENT_TYPE_FLOAT);

					writeAttribute(&Vertex::uv, attr);
				}
			}
			vertexOffset += mesh.subMeshes[j].vertexCount;

			// Copy index data
			if (primitive->indices != -1)
			{
				const tg3_accessor* accessor = &model.accessors[primitive->indices];
				const tg3_buffer_view* bufferView = &model.buffer_views[accessor->buffer_view];
				const tg3_buffer* buffer = &model.buffers[bufferView->buffer];

				assert(indexOffset + accessor->count <= sceneIndices.size() && "Not enough space for indices");

				mesh.subMeshes[j].indexStart = indexOffset;
				mesh.subMeshes[j].indexCount = accessor->count;

				if (accessor->component_type == TG3_COMPONENT_TYPE_UNSIGNED_INT)
				{
					const uint32_t* buffData = reinterpret_cast<const uint32_t*>(
						buffer->data.data + bufferView->byte_offset + accessor->byte_offset
					);
					memcpy(&sceneIndices[indexOffset], buffData, accessor->count * sizeof(uint32_t));
				}
				else if (accessor->component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT)
				{
					const uint16_t* buffData = reinterpret_cast<const uint16_t*>(
						buffer->data.data + bufferView->byte_offset + accessor->byte_offset
					);
					for (uint64_t k = 0; k < accessor->count; ++k)
					{
						sceneIndices[indexOffset + k] = static_cast<uint32_t>(buffData[k]);
					}
				}

				indexOffset += mesh.subMeshes[j].indexCount;
			}
		}

		sceneMeshes.push_back(std::move(mesh));
		meshIDs[i] = static_cast<uint32_t>(sceneMeshes.size());
	}

	std::cout << std::format("Loaded {} meshes.", meshIDs.size()) << std::endl;
	return meshIDs;
}

uint32_t Renderer::importNode(
	const tg3_model& model, int32_t nodeIndex, uint32_t parentID, uint32_t previousSiblingID, std::vector<uint32_t>& meshIDs
)
{
	const tg3_node& tg3Node = model.nodes[nodeIndex];

	// Create new node and set parent ID
	auto [node, nodeID] = scene.createNode();
	node.parentID = parentID;

	// Process transform
	if (tg3Node.has_matrix)
	{
		glm::mat4 transform(1);
		float* transformPointer = glm::value_ptr(transform);

		for (int i = 0; i < 16; ++i)
		{
			transformPointer[i] = static_cast<float>(tg3Node.matrix[i]);
		}

		node.setTransform(transform);
	}
	else
	{
		glm::vec3 translation(tg3Node.translation[0], tg3Node.translation[1], tg3Node.translation[2]);
		glm::quat rotation(tg3Node.rotation[3], tg3Node.rotation[0], tg3Node.rotation[1], tg3Node.rotation[2]);
		glm::vec3 scale(tg3Node.scale[0], tg3Node.scale[1], tg3Node.scale[2]);

		node.setTranslation(translation);
		node.setRotation(rotation);
		node.setScale(scale);
	}

	// Grab meshID if tg3node has valid mesh index
	if (tg3Node.mesh != -1) { node.meshID = meshIDs[tg3Node.mesh]; }

	// Link sibling nodes together
	if (previousSiblingID) { scene.getNode(previousSiblingID).nextSiblingID = nodeID; }

	// Iterate child nodes and recursively import
	uint32_t lastChildID = 0;
	for (uint32_t i = 0; i < tg3Node.children_count; ++i)
	{
		int32_t childIndex = tg3Node.children[i];
		lastChildID = importNode(model, childIndex, nodeID, lastChildID, meshIDs);

		// Set parent's first child ID
		if (!node.firstChildID) { node.firstChildID = lastChildID; }
	}

	return nodeID;
}