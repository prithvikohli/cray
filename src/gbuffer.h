#pragma once

#include "vk_graphics.h"

struct GBufferBundle
{
    std::shared_ptr<vk::ImageView> depth;
    std::shared_ptr<vk::ImageView> albedoMetallic;
    std::shared_ptr<vk::ImageView> normalRoughness;
    std::shared_ptr<vk::ImageView> emissive;
};

class GBufferPass
{
public:
    GBufferPass(vk::RenderContext* rc, const std::vector<uint32_t>& vertCode, const std::vector<uint32_t>& fragCode);
    GBufferPass(const GBufferPass&) = delete;

    ~GBufferPass();

    VkDescriptorSetLayout getDescriptorSetLayout() const { return m_descriptorSetLayout; }
    VkPipelineLayout getPipelineLayout() const { return m_pipelineLayout; }

    void bindBundle(GBufferBundle bundle);
    void begin(VkCommandBuffer cmdBuf) const;
    void end(VkCommandBuffer cmdBuf) const;

    GBufferPass& operator=(const GBufferPass&) = delete;

private:
    VkDevice m_device;
    VkRenderPass m_renderPass;
    VkDescriptorSetLayout m_descriptorSetLayout;
    VkPipelineLayout m_pipelineLayout;
    VkShaderModule m_vertModule;
    VkShaderModule m_fragModule;
    VkPipeline m_pipeline;

    VkExtent2D m_extent;

    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;

    void createRenderPass();
    void createLayouts();
    void createShaderModules(const std::vector<uint32_t>& vertCode, const std::vector<uint32_t>& fragCode);
    void createPipeline();
};