#pragma once

#include "vk_graphics.h"
#include "tiny_gltf.h"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

struct Material
{
    std::shared_ptr<vk::Image> albedo;
    std::shared_ptr<vk::Image> metallicRoughness;
    std::shared_ptr<vk::Image> normal;
    //std::shared_ptr<vk::Image> emissive;
};

struct Mesh
{
    uint32_t indexCount;
    std::shared_ptr<vk::Buffer> indexBuffer;
    std::shared_ptr<vk::Buffer> positionBuffer;
    std::shared_ptr<vk::Buffer> normalBuffer;
    std::shared_ptr<vk::Buffer> texCoordBuffer;

    Material* material;
};

struct Node
{
    Node* parent;
    Mesh* mesh;
    glm::mat4 localTransform;
    glm::mat4 recursiveTransform;
};

class Scene
{
public:
    Scene(vk::RenderContext* rc, const std::string& gltfBinaryFilename);
    Scene(const Scene&) = delete;

    ~Scene();

    Scene& operator=(const Scene&) = delete;

    std::vector<Node*> m_nodes;

private:
    vk::RenderContext* m_rc;

    std::vector<Material> m_materials;
    std::vector<Mesh> m_meshes;

    void createMaterial(tinygltf::Model& model, tinygltf::Material& material);
    void createMesh(tinygltf::Model& model, tinygltf::Mesh& mesh);
    void createNode(tinygltf::Model& model, tinygltf::Node& node, Node* parent = nullptr);
};