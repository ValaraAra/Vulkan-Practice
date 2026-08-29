#include "application.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

bool Application::initialize() {
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
		SDL_Log("SDL initialization failed: %s", SDL_GetError());
		return false;
	}

	window = SDL_CreateWindow("Vulkan Practice", width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

	if (!window) {
		SDL_Log("Window creation failed: %s", SDL_GetError());
		return false;
	}

	return true;
}

void Application::run() {
	running = true;

	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			// Handle exit/quit
			if (event.type == SDL_EVENT_QUIT) {
				running = false;
				break;
			}
			
			// Handle resize
			if (event.type == SDL_EVENT_WINDOW_RESIZED) {
				width = event.window.data1;
				height = event.window.data2;
				break;
			}
		}
	}
}

void Application::shutdown() {
	if (window) {
		SDL_DestroyWindow(window);
	}

	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}