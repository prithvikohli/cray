#pragma once

#include "vk_graphics.hpp"

class GBufferPass
{
public:
    GBufferPass(vk::RenderContext* rc, const std::vector<uint32_t>& vertCode, const std::vector<uint32_t>& fragCode);
    GBufferPass(const GBufferPass&) = delete;

    ~GBufferPass();

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

    void createRenderPass();
    void createLayouts();
    void createShaderModules(const std::vector<uint32_t>& vertCode, const std::vector<uint32_t>& fragCode);
    void createPipeline();
};