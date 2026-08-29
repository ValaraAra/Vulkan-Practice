cmake_minimum_required(VERSION 3.28)
project(VulkanPractice)

find_package(SDL3 REQUIRED)
find_package(Vulkan REQUIRED COMPONENTS volk shaderc_combined)
find_package(glm REQUIRED)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(vulkanapp
	"src/main.cpp"
	"src/application.h"
	"src/application.cpp"
	"src/renderer.h"
	"src/renderer.cpp"
)

if (MSVC)
	target_compile_options(vulkanapp PRIVATE /W4)
else()
	target_compile_options(vulkanapp PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
	target_compile_options(vulkanapp PRIVATE -Wno-missing-designated-field-initializers)
endif()

target_link_libraries(vulkanapp PRIVATE
	Vulkan::volk
	Vulkan::shaderc_combined
	SDL3::SDL3
	glm::glm
)