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
    VkDescriptorSetLayoutBinding outputBinding{};
    outputBinding.binding = 0u;
    outputBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    outputBinding.descriptorCount = 1u;
    outputBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding depthBinding{};
    depthBinding.binding = 1u;
    depthBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    depthBinding.descriptorCount = 1u;
    depthBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding albedoMetallicBinding{};
    albedoMetallicBinding.binding = 2u;
    albedoMetallicBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    albedoMetallicBinding.descriptorCount = 1u;
    albedoMetallicBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding normalRoughnessBinding{};
    normalRoughnessBinding.binding = 3u;
    normalRoughnessBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    normalRoughnessBinding.descriptorCount = 1u;
    normalRoughnessBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding uniformsBinding{};
    uniformsBinding.binding = 4u;
    uniformsBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformsBinding.descriptorCount = 1u;
    uniformsBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding emissiveBinding{};
    emissiveBinding.binding = 5u;
    emissiveBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    emissiveBinding.descriptorCount = 1u;
    emissiveBinding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutBinding bindings[] = { outputBinding, depthBinding, albedoMetallicBinding, normalRoughnessBinding, uniformsBinding, emissiveBinding };
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
    descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutInfo.bindingCount = ARRAY_LENGTH(bindings);
    descriptorSetLayoutInfo.pBindings = bindings;

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
