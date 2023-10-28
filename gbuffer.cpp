#include "gbuffer.hpp"

GBufferPass::GBufferPass(vk::RenderContext* rc, const std::vector<uint32_t>& vertCode, const std::vector<uint32_t>& fragCode) : m_device(rc->getDevice()), m_extent(rc->m_extent)
{
    createRenderPass();
    createLayouts();
    createShaderModules(vertCode, fragCode);
    createPipeline();
}

GBufferPass::~GBufferPass()
{
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    vkDestroyShaderModule(m_device, m_vertModule, nullptr);
    vkDestroyShaderModule(m_device, m_fragModule, nullptr);
    vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    vkDestroyRenderPass(m_device, m_renderPass, nullptr);
}

void GBufferPass::createRenderPass()
{
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentDescription albedoMetallicAttachment = depthAttachment;
    albedoMetallicAttachment.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription normalRoughnessAttachment = albedoMetallicAttachment;
    VkAttachmentDescription emissiveAttachment = albedoMetallicAttachment;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 0u;
    depthRef.attachment = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference albedoMetallicRef{};
    albedoMetallicRef.attachment = 1u;
    albedoMetallicRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference normalRoughnessRef{};
    normalRoughnessRef.attachment = 2u;
    normalRoughnessRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference emissiveRef{};
    emissiveRef.attachment = 3u;
    emissiveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachments[] = { albedoMetallicRef, normalRoughnessRef, emissiveRef };
    VkSubpassDescription subpassDesc{};
    subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDesc.colorAttachmentCount = 3u;
    subpassDesc.pColorAttachments = colorAttachments;
    subpassDesc.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = 0u;
    dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkAttachmentDescription attachments[] = { depthAttachment, albedoMetallicAttachment, normalRoughnessAttachment, emissiveAttachment };
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 4u;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1u;
    renderPassInfo.pSubpasses = &subpassDesc;
    renderPassInfo.dependencyCount = 1u;
    renderPassInfo.pDependencies = &dependency;

    VK_CHECK(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass), "failed to create GBuffer render pass!");
}

void GBufferPass::createLayouts()
{
    VkDescriptorSetLayoutBinding bindingInfo{};
    bindingInfo.binding = 0u;
    bindingInfo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindingInfo.descriptorCount = 1u;
    bindingInfo.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
    descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutInfo.bindingCount = 1u;
    descriptorSetLayoutInfo.pBindings = &bindingInfo;

    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &descriptorSetLayoutInfo, nullptr, &m_descriptorSetLayout), "failed to create GBuffer pipeline descriptor set layout!");

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1u;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;

    VK_CHECK(vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_pipelineLayout), "failed to create GBuffer pipeline layout!");
}

void GBufferPass::createShaderModules(const std::vector<uint32_t>& vertCode, const std::vector<uint32_t>& fragCode)
{
    VkShaderModuleCreateInfo shaderInfo{};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = vertCode.size() * 4u;
    shaderInfo.pCode = vertCode.data();

    VK_CHECK(vkCreateShaderModule(m_device, &shaderInfo, nullptr, &m_vertModule), "failed to create GBuffer pass vertex shader module!");

    shaderInfo.codeSize = fragCode.size() * 4u;
    shaderInfo.pCode = fragCode.data();

    VK_CHECK(vkCreateShaderModule(m_device, &shaderInfo, nullptr, &m_fragModule), "failed to create GBuffer pass fragment shader module!");
}

void GBufferPass::createPipeline()
{
    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = m_vertModule;
    vertStage.pName = "main";
    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = m_fragModule;
    fragStage.pName = "main";
    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage, fragStage };

    VkVertexInputBindingDescription vertexBindingPosition{};
    vertexBindingPosition.binding = 0u;
    vertexBindingPosition.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vertexBindingPosition.stride = 12u;
    VkVertexInputBindingDescription vertexBindingNormal{};
    vertexBindingPosition.binding = 1u;
    vertexBindingPosition.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vertexBindingPosition.stride = 12u;
    VkVertexInputBindingDescription vertexBindingTexCoord{};
    vertexBindingPosition.binding = 2u;
    vertexBindingPosition.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vertexBindingPosition.stride = 12u;
    VkVertexInputBindingDescription vertexBindings[] = { vertexBindingPosition, vertexBindingNormal, vertexBindingTexCoord };

    VkVertexInputAttributeDescription vertexAttributePosition{};
    vertexAttributePosition.location = 0u;
    vertexAttributePosition.binding = 0u;
    vertexAttributePosition.format = VK_FORMAT_R32G32B32_SFLOAT;
    VkVertexInputAttributeDescription vertexAttributeNormal{};
    vertexAttributePosition.location = 1u;
    vertexAttributePosition.binding = 1u;
    vertexAttributePosition.format = VK_FORMAT_R32G32B32_SFLOAT;
    VkVertexInputAttributeDescription vertexAttributeTexCoord{};
    vertexAttributePosition.location = 2u;
    vertexAttributePosition.binding = 2u;
    vertexAttributePosition.format = VK_FORMAT_R32G32_SFLOAT;
    VkVertexInputAttributeDescription vertexAttributes[] = { vertexAttributePosition, vertexAttributeNormal, vertexAttributeTexCoord };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 3u;
    vertexInputInfo.pVertexBindingDescriptions = vertexBindings;
    vertexInputInfo.vertexAttributeDescriptionCount = 3u;
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
    inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = m_extent.width;
    viewport.height = m_extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor;
    scissor.offset = { 0, 0 };
    scissor.extent = m_extent;
    VkPipelineViewportStateCreateInfo viewportInfo{};
    viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportInfo.viewportCount = 1u;
    viewportInfo.pViewports = &viewport;
    viewportInfo.scissorCount = 1u;
    viewportInfo.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizerInfo{};
    rasterizerInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizerInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizerInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizerInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizerInfo.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampleInfo{};
    multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState albedoMetallicBlendAttachmentInfo{};
    albedoMetallicBlendAttachmentInfo.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendAttachmentState normalRoughnessBlendAttachmentInfo = albedoMetallicBlendAttachmentInfo;
    VkPipelineColorBlendAttachmentState emissiveBlendAttachmentInfo = albedoMetallicBlendAttachmentInfo;
    VkPipelineColorBlendAttachmentState colorBlendAttachmentInfos[] = { albedoMetallicBlendAttachmentInfo, normalRoughnessBlendAttachmentInfo, emissiveBlendAttachmentInfo };

    VkPipelineColorBlendStateCreateInfo colorBlendInfo{};
    colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendInfo.attachmentCount = 3u;
    colorBlendInfo.pAttachments = colorBlendAttachmentInfos;

    VkPipelineDepthStencilStateCreateInfo depthInfo{};
    depthInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthInfo.depthTestEnable = VK_TRUE;
    depthInfo.depthWriteEnable = VK_TRUE;
    depthInfo.depthCompareOp = VK_COMPARE_OP_LESS;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2u;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
    pipelineInfo.pViewportState = &viewportInfo;
    pipelineInfo.pRasterizationState = &rasterizerInfo;
    pipelineInfo.pMultisampleState = &multisampleInfo;
    pipelineInfo.pDepthStencilState = &depthInfo;
    pipelineInfo.pColorBlendState = &colorBlendInfo;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0u;

    VK_CHECK(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1u, &pipelineInfo, nullptr, &m_pipeline), "failed to create GBuffer pass pipeline!");
}
