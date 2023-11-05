#include "lighting.h"

LightingPass::LightingPass(vk::RenderContext* rc, const std::vector<uint32_t>& lightingCode) : m_device(rc->getDevice())
{
    createShader(lightingCode);
    createLayouts();
    createPipeline();
}

LightingPass::~LightingPass()
{
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    vkDestroyShaderModule(m_device, m_shaderModule, nullptr);
}

void LightingPass::createShader(const std::vector<uint32_t>& lightingCode)
{
    VkShaderModuleCreateInfo shaderInfo{};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = lightingCode.size() * 4u;
    shaderInfo.pCode = lightingCode.data();

    VK_CHECK(vkCreateShaderModule(m_device, &shaderInfo, nullptr, &m_shaderModule), "failed to create lighting pass shader module!");
}

void LightingPass::createLayouts()
{
    //VkDescriptorSetLayoutBinding depthBinding{};
    //depthBinding.binding = 0u;
    //depthBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    //depthBinding.descriptorCount = 1u;
    //depthBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    //VkDescriptorSetLayoutBinding albedoMetallicBinding{};
    //albedoMetallicBinding.binding = 1u;
    //albedoMetallicBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    //albedoMetallicBinding.descriptorCount = 1u;
    //albedoMetallicBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    //VkDescriptorSetLayoutBinding normalRoughnessBinding{};
    //normalRoughnessBinding.binding = 2u;
    //normalRoughnessBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    //normalRoughnessBinding.descriptorCount = 1u;
    //normalRoughnessBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    //VkDescriptorSetLayoutBinding emissiveBinding{};
    //depthBinding.binding = 3u;
    //depthBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    //depthBinding.descriptorCount = 1u;
    //depthBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    //VkDescriptorSetLayoutBinding outputBinding{};
    //depthBinding.binding = 4u;
    //depthBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    //depthBinding.descriptorCount = 1u;
    //depthBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding outputBinding{};
    outputBinding.binding = 0u;
    outputBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    outputBinding.descriptorCount = 1u;
    outputBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
    descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutInfo.bindingCount = 1u;
    descriptorSetLayoutInfo.pBindings = &outputBinding;

    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &descriptorSetLayoutInfo, nullptr, &m_descriptorSetLayout), "failed to create lighting pass descriptor set layout!");

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1u;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;

    VK_CHECK(vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout), "failed to create lighting pass pipeline layout!");
}

void LightingPass::createPipeline()
{
    VkPipelineShaderStageCreateInfo shaderInfo{};
    shaderInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderInfo.module = m_shaderModule;
    shaderInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderInfo;
    pipelineInfo.layout = m_pipelineLayout;

    VK_CHECK(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1u, &pipelineInfo, nullptr, &m_pipeline), "failed to create lighting pass pipeline!");
}

void LightingPass::bindPipeline(VkCommandBuffer cmdBuf) const
{
    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
}