#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

class Node
{
  public:
	uint32_t meshID = 0;
	uint32_t parentID = 0;
	uint32_t nextSiblingID = 0;
	uint32_t firstChildID = 0;

	glm::vec3 getTranslation() const
	{ return translation; }
	void setTranslation(const glm::vec3& translation)
	{
		this->translation = translation;
		dirty = true;
	}

	glm::vec3 getScale() const
	{ return scale; }
	void setScale(const glm::vec3& scale)
	{
		this->scale = scale;
		dirty = true;
	}

	glm::quat getRotation() const
	{ return rotation; }
	void setRotation(const glm::quat& rotation)
	{
		this->rotation = rotation;
		dirty = true;
	}

	glm::mat4 getTransform()
	{
		// Recalculate local matrix transform if dirty
		if (dirty)
		{
			glm::mat4 matrixTranslate = glm::translate(glm::mat4(1), translation);
			glm::mat4 matrixRotate = glm::mat4_cast(rotation);
			glm::mat4 matrixScale = glm::scale(glm::mat4(1), scale);
			transform = matrixTranslate * matrixRotate * matrixScale;
			dirty = false;
		}

		return transform;
	}
	void setTransform(const glm::mat4& transform)
	{
		glm::vec3 skew;
		glm::vec4 perspective;
		glm::decompose(this->transform, scale, rotation, translation, skew, perspective);

		this->transform = transform;
		dirty = false;
	}

  private:
	glm::vec3 translation = glm::vec3(0);
	glm::vec3 scale = glm::vec3(1);
	glm::quat rotation = glm::quat(1, 0, 0, 0);
	glm::mat4 transform = glm::mat4(1);

	bool dirty = true;
};

class Scene
{
  public:
	void initialize(const size_t maxNodes)
	{
		this->maxNodes = maxNodes;
		nodes.reserve(maxNodes);
	}

	size_t getMaxNodes() const
	{ return maxNodes; }

	std::pair<Node&, uint32_t> createNode()
	{
		assert(nodes.size() < maxNodes && "Scene is at capacity!");

		nodes.push_back(Node{});

		uint32_t nodeID = static_cast<uint32_t>(nodes.size());
		return {nodes[nodeID - 1], nodeID};
	}

	Node& getNode(uint32_t nodeID)
	{
		assert(nodeID > 0 && "Invalid node ID!");

		return nodes[nodeID - 1];
	}

  private:
	std::vector<Node> nodes;
	size_t maxNodes = 0;
};
