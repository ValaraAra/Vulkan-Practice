#include "application.h"

#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>

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

	// Get Base Path
	const char* rawPath = SDL_GetBasePath();
	std::string basePath = rawPath ? rawPath : "";
	if (basePath.empty()) { throw std::runtime_error("SDL failed to grab base path!"); }

	// Load scene
	try
	{
		const std::string helmetPath = basePath + "assets/models/gltf/gltf-sample-assets-damaged-helmet/DamagedHelmet.gltf";
		const std::string sponzaPath = basePath + "assets/models/gltf/gltf-sample-assets-sponza/Sponza.gltf";
		renderer.loadData(helmetPath);
	}
	catch (const RenderError& error)
	{
		showError("Scene loading failed!\n\n" + std::string(error.what()));
		return false;
	}

	return true;
}

void Application::run()
{
	// Get key state and start time
	const bool* keys = SDL_GetKeyboardState(nullptr);
	uint64_t previousTime = SDL_GetTicks();

	// Game loop
	running = true;
	while (running)
	{
		// Handle events
		SDL_Event event{0};
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

		// Update time
		uint64_t currentTime = SDL_GetTicks();
		const float deltaTime = (currentTime - previousTime) / 1000.0f;
		previousTime = currentTime;

		// Handle keys
		constexpr float speed = 1.0f;
		constexpr float epsilon = 0.01f;
		constexpr float pitchLimit = glm::half_pi<float>() - epsilon;

		if (keys[SDL_SCANCODE_A]) { camYaw += speed * deltaTime; }
		if (keys[SDL_SCANCODE_D]) { camYaw -= speed * deltaTime; }
		if (keys[SDL_SCANCODE_W])
		{
			camPitch += speed * deltaTime;
			camPitch = std::clamp(camPitch, -pitchLimit, pitchLimit);
		}
		if (keys[SDL_SCANCODE_S])
		{
			camPitch -= speed * deltaTime;
			camPitch = std::clamp(camPitch, -pitchLimit, pitchLimit);
		}
		if (keys[SDL_SCANCODE_UP])
		{
			camDistance -= speed * deltaTime;
			camDistance = std::max(camDistance, epsilon);
		}
		if (keys[SDL_SCANCODE_DOWN]) { camDistance += speed * deltaTime; }

		// Update camera
		glm::vec3 camPosition =
			glm::vec3(std::cosf(camYaw) * cosf(camPitch), sinf(camPitch), sinf(camYaw) * cosf(camPitch)) * camDistance;

		const float aspectRatio = windowWidth / static_cast<float>(windowHeight);
		glm::mat4 viewMatrix = glm::lookAtRH(camPosition, glm::vec3(0), glm::vec3(0, 1, 0));
		glm::mat4 projectionMatrix = glm::perspectiveRH(glm::radians(75.0f), aspectRatio, 0.01f, 1000.0f);
		glm::mat4 viewProjectionMatrix = projectionMatrix * viewMatrix;

		// Render
		try
		{
			renderer.render(viewProjectionMatrix);
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

// SDL error message box
void Application::showError(const std::string& errorMessage)
{ SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", errorMessage.c_str(), window); }

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