#pragma once
#include "renderer.h"
#include <cstdint>

struct SDL_Window;

class Application {
  public:
	bool initialize();
	void run();
	void shutdown();

  private:
	SDL_Window *window = nullptr;
	Renderer renderer;

	static const int DEFAULT_WIDTH = 1280;
	static const int DEFAULT_HEIGHT = 720;

	uint32_t width = DEFAULT_WIDTH;
	uint32_t height = DEFAULT_HEIGHT;

	bool running = false;

	void showError(const std::string &errorMessage);
};