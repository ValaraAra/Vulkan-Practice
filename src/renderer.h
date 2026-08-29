#pragma once
#include <cstdint>
#include <vulkan/vulkan.h>

class Renderer {
  public:
	bool initialize(VkSurfaceKHR *surface, uint32_t *width, uint32_t *height);
	void render();
	void shutdown();

  private:
};