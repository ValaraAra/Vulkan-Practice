#include "application.h"
#include <SDL3/SDL.h>

void Application::showError(const std::string &errorMessage) {
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", errorMessage.c_str(), window);
}

bool Application::initialize() {
	// SDL initialization
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
		showError("SDL - Initialization failed! " + std::string(SDL_GetError()));
		return false;
	}

	// SDL window creation
	window = SDL_CreateWindow("Vulkan Practice", width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
	if (!window) {
		showError("SDL - Window creation failed! " + std::string(SDL_GetError()));
		return false;
	}

	// Renderer initialization
	std::string rendererMessage;
	if (!renderer.initialize(width, height, rendererMessage)) {
		showError("Renderer - Initialization failed: " + rendererMessage);
		return false;
	}

	return true;
}

void Application::run() {
	running = true;

	while (running) {
		// Handle events
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT) {
				running = false;
				break;
			}

			if (event.type == SDL_EVENT_WINDOW_RESIZED) {
				width = event.window.data1;
				height = event.window.data2;
				break;
			}
		}

		// Render
		renderer.render();
	}
}

void Application::shutdown() {
	if (window) {
		SDL_DestroyWindow(window);
	}

	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}