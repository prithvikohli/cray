#pragma once

#include "gbuffer.h"
#include "lighting.h"
#include "scene.h"

struct Camera
{
    glm::vec3 pos;
    glm::mat4 view;
    glm::mat4 proj;
};

struct Uniforms
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

struct MaterialViews
{
    std::shared_ptr<vk::ImageView> albedo;
    std::shared_ptr<vk::ImageView> metallicRoughness;
    std::shared_ptr<vk::ImageView> normal;
};

class DrawableNode
{
public:
    DrawableNode(Node* node, VkDevice device, VkPipelineLayout layout, VkDescriptorSet descriptorSet, std::shared_ptr<vk::Buffer> uniformsBuffer, MaterialViews matViews, VkSampler sampler);

    void update(Camera cam) const;
    void draw(VkCommandBuffer cmd) const;

private:
    Node* m_node;
    VkPipelineLayout m_layout;
    VkDescriptorSet m_descriptorSet;
    std::shared_ptr<vk::Buffer> m_uniformsBuffer;
    MaterialViews m_matViews;
};

struct LightingUniforms
{
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 camPos;
};

class Renderer
{
public:
    Renderer(vk::RenderContext* rc, const std::string& shadersDir);
    
    ~Renderer();

    void loadScene(const std::string& gltfBinaryFilename);
    void render() const;

    Camera m_camera;

private:
    vk::RenderContext* m_rc;
    VkDevice m_device;
    VkCommandBuffer m_cmdBuf;

    std::shared_ptr<vk::Image> m_depthImg;
    std::shared_ptr<vk::Image> m_albedoMetallicImg;
    std::shared_ptr<vk::Image> m_normalRoughnessImg;
    std::shared_ptr<vk::Image> m_emissiveImg;
    GBufferBundle m_gbuffer;

    std::shared_ptr<vk::Image> m_lightingImg;
    std::shared_ptr<vk::ImageView> m_lightingView;

    std::unique_ptr<GBufferPass> m_gbufferPass;
    std::unique_ptr<LightingPass> m_lightingPass;

    VkSemaphore m_imageAcquiredSemaphore;
    VkSemaphore m_renderFinishedSemaphore;
    VkFence m_inFlightFence;

    VkDescriptorPool m_descriptorPoolDrawables = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPoolLighting;
    VkDescriptorSet m_descriptorSetLighting;

    VkSampler m_samplerNearest;
    VkSampler m_samplerLinear;

    std::unique_ptr<Scene> m_scene;
    std::vector<DrawableNode> m_drawableNodes;

    std::shared_ptr<vk::Buffer> m_lightingUniforms;

    void createSyncObjects();
    void createLightingDescriptorSet();
    void createSamplers();

    DrawableNode createDrawableNode(Node* node) const;

    std::vector<uint32_t> readShader(const std::string& filename) const;
};