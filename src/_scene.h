#pragma once

#include "vk_graphics.h"
#include "tiny_gltf.h"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>

struct Material
{
    // TODO below should be clearly named as textures, e.g. texAlbedo
    // and also include flat values if available, e.g. vec3 albedo
    std::shared_ptr<vk::Image> albedo;
    std::shared_ptr<vk::Image> metallicRoughness;
    std::shared_ptr<vk::Image> normal;
    std::shared_ptr<vk::Image> emissive;
};

// TODO maybe this should be the only thing exposed (i.e. this should be Material, rather than above) - store scene images somewhere else
struct MaterialViews
{
    std::shared_ptr<vk::ImageView> albedo;
    std::shared_ptr<vk::ImageView> metallicRoughness;
    std::shared_ptr<vk::ImageView> normal;
    std::shared_ptr<vk::ImageView> emissive;
};

struct Mesh
{
    uint32_t indexCount;
    VkIndexType indexType;
    std::shared_ptr<vk::Buffer> indexBuffer;
    std::shared_ptr<vk::Buffer> positionBuffer;
    std::shared_ptr<vk::Buffer> normalBuffer;
    std::shared_ptr<vk::Buffer> tangentBuffer;
    std::shared_ptr<vk::Buffer> texCoordBuffer;

    MaterialViews* material;
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
    Scene(vk::RenderContext* rc, const std::string& gltfFilename, bool binary = false);
    Scene(const Scene&) = delete;

    ~Scene();

    Scene& operator=(const Scene&) = delete;

    std::vector<Mesh> m_meshes;
    std::vector<Node*> m_nodes;

private:
    vk::RenderContext* m_rc;

    std::vector<Material> m_materials;
    std::vector<MaterialViews> m_materialViews;

    void createMaterial(tinygltf::Model& model, tinygltf::Material& material);
    void createMesh(tinygltf::Model& model, tinygltf::Mesh& mesh);
    void createNode(tinygltf::Model& model, tinygltf::Node& node, Node* parent = nullptr);
};

namespace vk
{

class AccelerationStructure
{
public:
    AccelerationStructure(RenderContext* rc, const Scene& scene);
    AccelerationStructure(const AccelerationStructure&) = delete;

    ~AccelerationStructure();

    VkAccelerationStructureKHR getTlas() const { return m_tlas; }

    AccelerationStructure& operator=(const AccelerationStructure&) = delete;

private:
    void buildBlases(const Scene& scene);
    void buildTlas(const Scene& scene);

    RenderContext* m_rc;
    std::vector<std::shared_ptr<vk::Buffer>> m_blasBuffers;
    std::map<const Mesh*, VkAccelerationStructureKHR> m_blases;

    std::shared_ptr<vk::Buffer> m_tlasBuffer;
    VkAccelerationStructureKHR m_tlas;
};

}
