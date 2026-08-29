cmake_minimum_required(VERSION 3.28)
project(VulkanPractice)

find_package(SDL3 REQUIRED)
find_package(Vulkan REQUIRED COMPONENTS volk shaderc_combined)
find_package(glm REQUIRED)

add_executable(vulkanapp
	"src/main.cpp"
	"src/application.h"
	"src/application.cpp"
)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
	target_compile_options(vulkanapp PRIVATE /W4 /WX)
else()
	target_compile_options(vulkanapp PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

target_link_libraries(vulkanapp PRIVATE
	Vulkan::Vulkan
	Vulkan::volk
	Vulkan::shaderc_combined
	SDL3::SDL3
	glm::glm
)