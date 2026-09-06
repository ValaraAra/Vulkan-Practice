#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

struct Image
{
	int width;
	int height;
	int channels;
	unsigned char* data;
};

struct Texture
{
	uint32_t imageID = 0;
	uint32_t samplerID = 0;
};

struct Material
{
	glm::vec4 baseColor = glm::vec4(1.0f);
	uint32_t textureID = 0;
};

struct Vertex
{
	glm::vec3 position = glm::vec3(0.0f);
	glm::vec3 color = glm::vec3(1.0f);
	glm::vec3 normal = glm::vec3(0.0f);
	glm::vec2 uv = glm::vec2(0.0f);
};

struct SubMesh
{
	size_t vertexStart = 0;
	size_t vertexCount = 0;
	size_t indexStart = 0;
	size_t indexCount = 0;
	uint32_t materialID = 0;
};

struct Mesh
{
	std::string name;
	std::vector<SubMesh> subMeshes;
};
