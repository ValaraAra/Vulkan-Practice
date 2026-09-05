#pragma once
#include "renderer.h"

#include <cstdint>

struct SDL_Window;

class Application
{
  public:
	bool initialize();
	void run();
	void shutdown();

  private:
	SDL_Window* window = nullptr;
	Renderer renderer;

	static const uint32_t DEFAULT_WIDTH = 1280;
	static const uint32_t DEFAULT_HEIGHT = 720;

	bool running = false;

	void showError(const std::string& errorMessage);
};