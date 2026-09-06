cmake_minimum_required(VERSION 3.28)
project(VulkanPractice)

find_package(SDL3 REQUIRED)
find_package(Vulkan REQUIRED COMPONENTS volk shaderc_combined)
find_package(glm REQUIRED)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(vulkanapp
	"${CMAKE_SOURCE_DIR}/src/main.cpp"
	"${CMAKE_SOURCE_DIR}/src/application.h"
	"${CMAKE_SOURCE_DIR}/src/application.cpp"
	"${CMAKE_SOURCE_DIR}/src/renderer.h"
	"${CMAKE_SOURCE_DIR}/src/renderer.cpp"
	"${CMAKE_SOURCE_DIR}/src/utility.h"
	"${CMAKE_SOURCE_DIR}/src/utility.cpp"
	"${CMAKE_SOURCE_DIR}/external/tinygltf.cpp"
)

if (MSVC)
	target_compile_options(vulkanapp PRIVATE /W4)
else()
	target_compile_options(vulkanapp PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(vulkanapp PRIVATE
	Vulkan::volk
	Vulkan::shaderc_combined
	SDL3::SDL3
	glm::glm
)

target_include_directories(vulkanapp PRIVATE "${CMAKE_SOURCE_DIR}/external/")

# Copy resources (shaders and assets) to build directory
file(GLOB_RECURSE RUNTIME_RESOURCE_FILES CONFIGURE_DEPENDS
	LIST_DIRECTORIES false
	"${CMAKE_SOURCE_DIR}/shaders/*"
	"${CMAKE_SOURCE_DIR}/assets/*"
)

set(RUNTIME_RESOURCE_STAMP "${CMAKE_BINARY_DIR}/runtime_resources.stamp")
add_custom_command(OUTPUT "${RUNTIME_RESOURCE_STAMP}"
	DEPENDS ${RUNTIME_RESOURCE_FILES}
	COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:vulkanapp>/shaders"
	COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:vulkanapp>/assets"
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:vulkanapp>/shaders"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_SOURCE_DIR}/shaders"
		"$<TARGET_FILE_DIR:vulkanapp>/shaders"
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:vulkanapp>/assets"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_SOURCE_DIR}/assets"
		"$<TARGET_FILE_DIR:vulkanapp>/assets"
	COMMAND ${CMAKE_COMMAND} -E touch "${RUNTIME_RESOURCE_STAMP}"
)

add_custom_target(vulkanapp_resources DEPENDS "${RUNTIME_RESOURCE_STAMP}")
add_dependencies(vulkanapp vulkanapp_resources)