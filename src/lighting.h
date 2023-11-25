#pragma once

#include "vk_graphics.h"

class LightingPass
{
public:
    LightingPass(vk::RenderContext* rc, const std::vector<uint32_t>& lightingCode);
    LightingPass(const LightingPass&) = delete;

    ~LightingPass();

    void bindPipeline(VkCommandBuffer cmdBuf) const;
    VkDescriptorSetLayout getDescriptorSetLayout() const { return m_descriptorSetLayout; }
    VkPipelineLayout getPipelineLayout() const { return m_pipelineLayout; }

    LightingPass& operator=(const LightingPass&) = delete;

private:
    VkDevice m_device;
    VkShaderModule m_shaderModule;
    VkDescriptorSetLayout m_descriptorSetLayout;
    VkPipelineLayout m_pipelineLayout;
    VkPipeline m_pipeline;

    void createShader(const std::vector<uint32_t>& lightingCode);
    void createLayouts();
    void createPipeline();
};