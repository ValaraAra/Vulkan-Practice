#include "application.h"

#include <SDL3/SDL.h>

// SDL error message box
void Application::showError(const std::string& errorMessage)
{ SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", errorMessage.c_str(), window); }

bool Application::initialize()
{
	// SDL initialization
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
	{
		showError("SDL initialization failed! " + std::string(SDL_GetError()));
		return false;
	}

	// SDL window creation
	window = SDL_CreateWindow("Vulkan Practice", DEFAULT_WIDTH, DEFAULT_HEIGHT, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
	if (!window)
	{
		showError("SDL window creation failed! " + std::string(SDL_GetError()));
		return false;
	}

	// Renderer initialization
	try
	{
		renderer.initialize(window);
	}
	catch (const RenderError& error)
	{
		showError("Renderer initialization failed!\n\n" + std::string(error.what()));

		return false;
	}

	return true;
}

// Returns false if further event handling should stop early
bool Application::handleEvent(SDL_Event& event)
{
	if (event.type == SDL_EVENT_QUIT)
	{
		running = false;
		return false;
	}

	if (event.type == SDL_EVENT_WINDOW_RESIZED)
	{
		renderer.invalidateSwapchain();
		return true;
	}

	return true;
}

void Application::run()
{
	running = true;

	while (running)
	{
		// Handle events
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (!handleEvent(event)) { break; }
		}

		// Running flag may have changed
		if (!running) { break; }

		// Skip rendering if the window doesn't have a valid size (minimized or resized to 0 width/height)
		int windowWidth, windowHeight;
		if (!SDL_GetWindowSizeInPixels(window, &windowWidth, &windowHeight) || windowWidth == 0 || windowHeight == 0
			|| (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED))
		{
			if (SDL_WaitEventTimeout(&event, 100)) { handleEvent(event); }

			continue;
		}

		// Render
		try
		{
			renderer.render();
		}
		catch (const RenderError& error)
		{
			showError("Rendering failed!\n\n" + std::string(error.what()));

			running = false;
			break;
		}
	}
}

void Application::shutdown()
{
	renderer.shutdown();

	if (window) { SDL_DestroyWindow(window); }

	SDL_Quit();
}