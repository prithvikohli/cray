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
    //std::vector<VkImage> m_swapchainImages;
    //std::vector<VkImageView> m_swapchainViews;
    std::shared_ptr<vk::Image> m_lightingImg;
    std::shared_ptr<vk::ImageView> m_lightingView;

    std::unique_ptr<LightingPass> m_lightingPass;
    VkSemaphore m_imageAcquiredSemaphore;
    VkSemaphore m_renderFinishedSemaphore;
    VkFence m_inFlightFence;
    VkDescriptorPool m_descriptorPool;
    VkDescriptorSet m_descriptorSet;

    void createSyncObjects();
    void createDescriptorSet();
    std::vector<uint32_t> readShader(const std::string& filename) const;
};