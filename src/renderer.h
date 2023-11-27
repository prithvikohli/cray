#pragma once

#include "gbuffer.h"
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

class DrawableNode
{
public:
    DrawableNode(Node* node, std::shared_ptr<vk::DescriptorSet> descriptorSet, std::shared_ptr<vk::Buffer> uniformsBuffer, VkSampler sampler);

    void update(Camera cam) const;
    void draw(VkCommandBuffer cmd, VkPipelineLayout layout) const;

private:
    Node* m_node;
    std::shared_ptr<vk::DescriptorSet> m_descriptorSet;
    std::shared_ptr<vk::Buffer> m_uniformsBuffer;
};

struct GBufferBundle
{
    std::shared_ptr<vk::ImageView> depth;
    std::shared_ptr<vk::ImageView> albedoMetallic;
    std::shared_ptr<vk::ImageView> normalRoughness;
    std::shared_ptr<vk::ImageView> emissive;
};

struct LightingUniforms
{
    glm::mat4 invViewProj;
    glm::vec3 viewPos;
    uint32_t pad;
    glm::vec2 invRes;
};

class Renderer
{
public:
    Renderer(vk::RenderContext* rc, const std::string& shadersDir);
    
    ~Renderer();

    void loadScene(const std::string& gltfFilename, bool binary, const std::string& envmapHdrFilename);
    void render() const;

    Camera m_camera;

private:
    vk::RenderContext* m_rc;
    std::string m_shadersDir;
    VkDevice m_device;
    vk::CommandBuffer m_cmdBuf;

    std::shared_ptr<vk::Image> m_depthImg;
    std::shared_ptr<vk::Image> m_albedoMetallicImg;
    std::shared_ptr<vk::Image> m_normalRoughnessImg;
    std::shared_ptr<vk::Image> m_emissiveImg;
    GBufferBundle m_gbuffer;

    VkFramebuffer m_framebuf;

    std::shared_ptr<vk::Image> m_lightingImg;
    std::shared_ptr<vk::ImageView> m_lightingView;

    std::unique_ptr<GBufferPass> m_gbufferPass;
    std::unique_ptr<vk::PipelineLayout> m_lightingPipelineLayout;
    VkPipeline m_lightingPipeline;
    std::unique_ptr<vk::DescriptorPool> m_lightingDescriptorPool;
    std::shared_ptr<vk::DescriptorSet> m_lightingDescriptorSet;

    VkSemaphore m_imageAcquiredSemaphore;
    VkSemaphore m_renderFinishedSemaphore;
    VkFence m_inFlightFence;

    std::unique_ptr<vk::DescriptorPool> m_drawablesDescriptorPool;

    VkSampler m_samplerNearest;
    VkSampler m_samplerLinear;

    std::unique_ptr<Scene> m_scene;
    std::vector<DrawableNode> m_drawableNodes;
    std::unique_ptr<vk::AccelerationStructure> m_AS;

    std::shared_ptr<vk::Buffer> m_lightingUniforms;

    std::shared_ptr<vk::Image> m_envMapImg;
    std::shared_ptr<vk::ImageView> m_envMapView;

    void createGBuffer();
    void setupLightingPass();
    void createSyncObjects();
    void createSamplers();

    DrawableNode createDrawableNode(Node* node) const;

    std::vector<uint32_t> readShader(const std::string& filename) const;
};