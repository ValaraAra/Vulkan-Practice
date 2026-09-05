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
	window = SDL_CreateWindow("Vulkan Practice", width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
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
		renderer.shutdown();

		showError("Renderer initialization failed!\n\n" + std::string(error.what()));

		return false;
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
			if (event.type == SDL_EVENT_QUIT)
			{
				running = false;
				break;
			}

			if (event.type == SDL_EVENT_WINDOW_RESIZED)
			{
				width = event.window.data1;
				height = event.window.data2;
				renderer.invalidateSwapchain();
				break;
			}
		}

		// Render
		try
		{
			renderer.render();
		}
		catch (const RenderError& error)
		{
			renderer.shutdown();

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