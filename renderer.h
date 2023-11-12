#pragma once

#include "gbuffer.h"
#include "lighting.h"
#include "scene.h"

class Renderer
{
public:
    Renderer(vk::RenderContext* rc, const std::string& shadersDir);
    
    ~Renderer();

    void loadScene(const std::string& gltfBinaryFilename);
    void render() const;

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
    VkDescriptorPool m_descriptorPool;
    VkDescriptorSet m_descriptorSet;

    std::unique_ptr<Scene> m_scene;

    void createSyncObjects();
    void createDescriptorSet();
    std::vector<uint32_t> readShader(const std::string& filename) const;
};