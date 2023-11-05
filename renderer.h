#pragma once

#include "lighting.h"

class Renderer
{
public:
    Renderer(vk::RenderContext* rc, const std::string& shadersDir);
    
    ~Renderer();

    void render() const;

private:
    vk::RenderContext* m_rc;
    VkDevice m_device;
    VkCommandBuffer m_cmdBuf;
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainViews;
    VkExtent2D m_extent;

    std::unique_ptr<LightingPass> m_lightingPass;
    VkSemaphore m_imageAcquiredSemaphore;
    VkSemaphore m_renderFinishedSemaphore;
    VkFence m_inFlightFence;
    VkDescriptorPool m_descriptorPool;
    std::vector<VkDescriptorSet> m_descriptorSets;

    void createSyncObjects();
    void createDescriptorSets();
    std::vector<uint32_t> readShader(const std::string& filename) const;
};