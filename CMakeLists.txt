cmake_minimum_required(VERSION 3.28)
project(VulkanRenderer)

find_package(SDL3 REQUIRED)
find_package(Vulkan REQUIRED COMPONENTS volk shaderc_combined)
find_package(glm REQUIRED)
find_package(Tracy CONFIG REQUIRED)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TRACY_ENABLE "Enable Tracy profiler." OFF)

add_executable(vulkanapp
	"${CMAKE_SOURCE_DIR}/src/main.cpp"
	"${CMAKE_SOURCE_DIR}/src/application.h"
	"${CMAKE_SOURCE_DIR}/src/application.cpp"
	"${CMAKE_SOURCE_DIR}/src/renderer.h"
	"${CMAKE_SOURCE_DIR}/src/renderer.cpp"
	"${CMAKE_SOURCE_DIR}/src/utility.h"
	"${CMAKE_SOURCE_DIR}/src/utility.cpp"
	"${CMAKE_SOURCE_DIR}/src/resources.h"
	"${CMAKE_SOURCE_DIR}/external/tinygltf.cpp"
)

if (MSVC)
	target_compile_options(vulkanapp PRIVATE /W4)
else()
	target_compile_options(vulkanapp PRIVATE -Wall -Wextra -Wpedantic)
endif()

get_target_property(TRACY_INCLUDE_DIR Tracy::TracyClient INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(vulkanapp PRIVATE "${CMAKE_SOURCE_DIR}/external" "${TRACY_INCLUDE_DIR}")

target_compile_definitions(vulkanapp PRIVATE
	VK_NO_PROTOTYPES
	GLM_FORCE_DEPTH_ZERO_TO_ONE
)

target_link_libraries(vulkanapp PRIVATE
	Vulkan::volk
	Vulkan::shaderc_combined
	SDL3::SDL3
	glm::glm
)

if(TRACY_ENABLE)
	target_link_libraries(vulkanapp PRIVATE Tracy::TracyClient)
endif()

# Copy resources (shaders and assets) to build directory
file(GLOB_RECURSE SHADERS_RESOURCE_FILES CONFIGURE_DEPENDS
	LIST_DIRECTORIES false
	"${CMAKE_SOURCE_DIR}/shaders/*"
)

file(GLOB_RECURSE ASSETS_RESOURCE_FILES CONFIGURE_DEPENDS
	LIST_DIRECTORIES false
	"${CMAKE_SOURCE_DIR}/assets/*"
)

set(SHADERS_RESOURCE_STAMP "${CMAKE_BINARY_DIR}/shaders_resources.stamp")
set(ASSETS_RESOURCE_STAMP "${CMAKE_BINARY_DIR}/assets_resources.stamp")

add_custom_command(OUTPUT "${SHADERS_RESOURCE_STAMP}"
	DEPENDS ${SHADERS_RESOURCE_FILES}
	COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:vulkanapp>/shaders"
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:vulkanapp>/shaders"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_SOURCE_DIR}/shaders"
		"$<TARGET_FILE_DIR:vulkanapp>/shaders"
	COMMAND ${CMAKE_COMMAND} -E touch "${SHADERS_RESOURCE_STAMP}"
)

add_custom_command(OUTPUT "${ASSETS_RESOURCE_STAMP}"
	DEPENDS ${ASSETS_RESOURCE_FILES}
	COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:vulkanapp>/assets"
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:vulkanapp>/assets"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_SOURCE_DIR}/assets"
		"$<TARGET_FILE_DIR:vulkanapp>/assets"
	COMMAND ${CMAKE_COMMAND} -E touch "${ASSETS_RESOURCE_STAMP}"
)

add_custom_target(vulkanapp_shaders DEPENDS "${SHADERS_RESOURCE_STAMP}")
add_custom_target(vulkanapp_assets DEPENDS "${ASSETS_RESOURCE_STAMP}")

add_dependencies(vulkanapp vulkanapp_shaders vulkanapp_assets)