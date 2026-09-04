#include "renderer.h"

#include "utility.h"

#include <SDL3/SDL_vulkan.h>
#include <iostream>
#include <vector>

#define VOLK_IMPLEMENTATION
#include <Volk/volk.h>

#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

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

bool Renderer::initialize(SDL_Window* window, std::function<void(const std::string&)> errorCallback)
{
	this->window = window;
	this->errorCallback = errorCallback;

	if (volkInitialize() != VK_SUCCESS)
	{
		errorCallback("Error initializing Volk.");
		return false;
	}

	if (!createVulkanInstance())
	{
		errorCallback("Error creating Vulkan instance.");
		return false;
	}

	volkLoadInstance(vulkanInstance);

	if (!createSurface())
	{
		errorCallback("Error creating vulkan window surface.");
		return false;
	}

	physicalDevice = selectPhysicalDevice();
	if (physicalDevice == VK_NULL_HANDLE)
	{
		errorCallback("Error selecting physical device.");
		return false;
	}

	if (!selectGraphicsQueue())
	{
		errorCallback("Error selecting graphics queue.");
		return false;
	}

	if (!createDevice(physicalDevice))
	{
		errorCallback("Error creating logical device.");
		return false;
	}

	if (!initializeVMA())
	{
		errorCallback("Error initializing VMA.");
		return false;
	}

	int width, height;
	SDL_GetWindowSize(window, &width, &height);
	if (width == NULL || height == NULL)
	{
		errorCallback("Error getting window size.");
		return false;
	}

	if (!createSwapchain(width, height))
	{
		errorCallback("Error creating swapchain.");
		return false;
	}

	if (!createShaders())
	{
		errorCallback("Error creating shaders.");
		return false;
	}

	pipeline = createGraphicsPipeline();
	if (pipeline == VK_NULL_HANDLE)
	{
		errorCallback("Error creating graphics pipeline.");
		return false;
	}

	if (!createSyncResources())
	{
		errorCallback("Error creating synchronization resources.");
		return false;
	}

	if (!createCommandBuffers())
	{
		errorCallback("Error creating command buffers.");
		return false;
	}

	return true;
}

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

	return vkCreateInstance(&instanceCreateInfo, nullptr, &vulkanInstance) == VK_SUCCESS;
}

bool Renderer::createSurface()
{
	if (!SDL_Vulkan_CreateSurface(window, vulkanInstance, nullptr, &surface))
	{
		errorCallback("Vulkan surface creation failed!\n\n" + std::string(SDL_GetError()));
		return false;
	}
	return true;
}

// Defaults to first device, but will try to find a discrete GPU if available.
// - Replace with a more sophisticated selection algorithm at some point!
VkPhysicalDevice Renderer::selectPhysicalDevice()
{
	uint32_t physicalDeviceCount = 0;
	vkEnumeratePhysicalDevices(vulkanInstance, &physicalDeviceCount, nullptr);

	std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
	vkEnumeratePhysicalDevices(vulkanInstance, &physicalDeviceCount, physicalDevices.data());

	if (physicalDeviceCount == 0)
	{
		errorCallback("No physical devices found.");
		return VK_NULL_HANDLE;
	}

	// Default to first device
	VkPhysicalDevice physicalDevice = physicalDevices[0];

	// Try to find a discrete GPU
	for (const auto& device : physicalDevices)
	{
		VkPhysicalDeviceProperties deviceProperties{};
		vkGetPhysicalDeviceProperties(device, &deviceProperties);

		if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
		{
			physicalDevice = device;
			break;
		}
	}

	// Print selected device name
	VkPhysicalDeviceProperties props{};
	vkGetPhysicalDeviceProperties(physicalDevice, &props);
	std::cout << "Selected physical device: " << props.deviceName << std::endl;

	// Ensure the requested swapchain format is supported
	uint32_t formatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);

	std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, surfaceFormats.data());

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
		errorCallback("Requested swapchain format not supported by the selected physical device and surface combination.");
		return VK_NULL_HANDLE;
	}

	return physicalDevice;
}

bool Renderer::selectGraphicsQueue()
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
			return true;
		}
	}

	return false;
}

bool Renderer::createDevice(VkPhysicalDevice physicalDevice)
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
		|| !supportedFeatures12.timelineSemaphore)
	{
		errorCallback("Physical device does not support required features.");
		return false;
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
		.timelineSemaphore = VK_TRUE,
	};
	VkPhysicalDeviceFeatures2 enabledFeatures{
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
		.pNext = &enabledFeatures12,
	};

	// Request queues
	std::vector<float> queuePriorities{1.0f};
	VkDeviceQueueCreateInfo graphicsQueueInfo{
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = graphicsQueueFamilyIndex,
		.queueCount = 1,
		.pQueuePriorities = queuePriorities.data(),
	};

	// Device extensions
	const std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

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
		errorCallback("Failed to create logical device.");
		return false;
	}

	// Get the graphics queue
	vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
	if (!graphicsQueue)
	{
		errorCallback("Failed to get graphics queue.");
		return false;
	}

	return true;
}

bool Renderer::initializeVMA()
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
		errorCallback("Failed to create VMA allocator.");
		return false;
	}

	return true;
}

bool Renderer::createSwapchain(uint32_t width, uint32_t height)
{
	swapchainWidth = width;
	swapchainHeight = height;

	// Get surface capabilities
	VkSurfaceCapabilitiesKHR surfaceCapabilities{};
	if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCapabilities) != VK_SUCCESS)
	{
		errorCallback("Failed to get surface capabilities.");
		return false;
	}

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
		errorCallback("Failed to create swapchain.");
		return false;
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
			errorCallback(std::format("Failed to create image view for swapchain image {}.", i));
			return false;
		}
	}

	// Create semaphore for each swapchain image
	renderCompleteSemaphores.resize(swapchainImages.size());
	for (size_t i = 0; i < renderCompleteSemaphores.size(); ++i)
	{
		VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

		if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderCompleteSemaphores[i]) != VK_SUCCESS)
		{
			errorCallback(std::format("Failed to create render-complete semaphore for swapchain image {}.", i));
			return false;
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
		errorCallback("Failed to create depth image.");
		return false;
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
		errorCallback("Failed to create image view for depth image.");
		return false;
	}

	return true;
}

void Renderer::destroySwapchain()
{
	for (VkImageView imageView : swapchainImageViews)
	{
		vkDestroyImageView(device, imageView, nullptr);
	}

	for (VkSemaphore& semaphore : renderCompleteSemaphores)
	{
		vkDestroySemaphore(device, semaphore, nullptr);
	}

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

	if (shaderSource.empty())
	{
		errorCallback("Failed to read shader source for \"" + filename + "\".");
		return VK_NULL_HANDLE;
	}

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
		errorCallback("Failed to compile shader: " + filename + ".\n\n" + result.GetErrorMessage());
		return VK_NULL_HANDLE;
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
		errorCallback("Failed to create shader module for \"" + filename + "\".");
		return VK_NULL_HANDLE;
	}

	return shaderModule;
}

bool Renderer::createShaders()
{
	// Vertex shader
	vertShader = createShaderModule("shader.vert", shaderc_vertex_shader);
	if (vertShader == VK_NULL_HANDLE) { return false; }

	// Fragment shader
	fragShader = createShaderModule("shader.frag", shaderc_fragment_shader);
	if (fragShader == VK_NULL_HANDLE) { return false; }

	return true;
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
		errorCallback("Failed to create pipeline layout.");
		return VK_NULL_HANDLE;
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
		errorCallback("Failed to create graphics pipeline.");
		return VK_NULL_HANDLE;
	}

	return pipeline;
}

bool Renderer::createSyncResources()
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
		errorCallback("Failed to create timeline semaphore.");
		return false;
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
			errorCallback("Failed to create per-frame image-acquired semaphore.");
			return false;
		}
	}

	return true;
}

bool Renderer::createCommandBuffers()
{
	for (FrameResources& frameResource : frameResources)
	{
		// Create per-frame command pool
		VkCommandPoolCreateInfo commandPoolInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
			.queueFamilyIndex = graphicsQueueFamilyIndex,
		};
		if (vkCreateCommandPool(device, &commandPoolInfo, nullptr, &frameResource.commandPool) != VK_SUCCESS)
		{
			errorCallback("Failed to create per-frame command pool.");
			return false;
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
			errorCallback("Failed to allocate per-frame command buffer.");
			return false;
		}
	}

	return true;
}

void Renderer::render()
{
	// First check swapchain validity
	if (requireSwapchainRecreation)
	{
		vkDeviceWaitIdle(device);

		destroySwapchain();

		int width, height;
		SDL_GetWindowSize(window, &width, &height);
		createSwapchain(width, height);

		requireSwapchainRecreation = false;
	}

	const uint32_t frameResourceIndex = frameIndex++ % MaxFramesInFlight;
	const uint64_t signalValue = nextSignalValue++;
	const uint64_t waitValue = signalValue - MaxFramesInFlight;

	// Ensre its safe to start recording commands for this frame resource
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

	// Acquire the next swapchain image
	VkSemaphore imageAcquiredSemaphore = frameResource.imageAcquiredSemaphore;

	uint32_t swapchainImageIndex;
	VkResult acquireResult =
		vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAcquiredSemaphore, VK_NULL_HANDLE, &swapchainImageIndex);

	// Handle swapchain recreation if needed
	if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR)
	{
		requireSwapchainRecreation = true;
		return;
	} else if (acquireResult != VK_SUCCESS)
	{
		errorCallback("Failed to acquire next swapchain image.");
		return;
	}

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
		.clearValue{.color{0.01f, 0.01f, 0.01f, 1.0f}},
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
		errorCallback("Failed to record command buffer.");
		return;
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
	vkQueueSubmit2(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);

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
	vkQueuePresentKHR(graphicsQueue, &presentInfo);
}

void Renderer::shutdown()
{
	// Flush GPU first
	vkDeviceWaitIdle(device);

	// Frame/sync resources
	if (timelineSemaphore)
	{
		vkDestroySemaphore(device, timelineSemaphore, nullptr);
		timelineSemaphore = VK_NULL_HANDLE;
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
		vmaAllocator = VK_NULL_HANDLE;
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

	if (vulkanInstance)
	{
		vkDestroyInstance(vulkanInstance, nullptr);
		vulkanInstance = VK_NULL_HANDLE;
	}
	volkFinalize();
}
