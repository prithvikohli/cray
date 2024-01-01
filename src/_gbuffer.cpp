#include "gbuffer.h"

GBufferPass::GBufferPass(vk::RenderContext* rc, const vk::Image* depthAttachment, const std::vector<const vk::Image*>& colorAttachments, const std::vector<uint32_t>& vertCode, const std::vector<uint32_t>& fragCode) : m_device(rc->getDevice()), m_extent(rc->m_extent), m_colorAttachmentCount(colorAttachments.size())
{
    createRenderPass(depthAttachment, colorAttachments);
    createPipelineLayout(vertCode, fragCode);
    createPipeline(vertCode, fragCode);
}

GBufferPass::~GBufferPass()
{
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    vkDestroyRenderPass(m_device, m_renderPass, nullptr);
}

void GBufferPass::createRenderPass(const vk::Image* depthImage, const std::vector<const vk::Image*>& colorImages)
{
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthImage->m_imageInfo.format;
    depthAttachment.samples = depthImage->m_imageInfo.samples;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    std::vector<VkAttachmentDescription> colorAttachments;
    colorAttachments.reserve(m_colorAttachmentCount);
    for (const vk::Image* img : colorImages)
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = img->m_imageInfo.format;
        colorAttachment.samples = img->m_imageInfo.samples;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        colorAttachments.push_back(colorAttachment);
    }

    VkAttachmentReference depthRef{};
    depthRef.attachment = 0u;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    std::vector<VkAttachmentReference> colorRefs;
    colorRefs.reserve(m_colorAttachmentCount);
    for (uint32_t i = 1; i < m_colorAttachmentCount + 1u; i++)
    {
        VkAttachmentReference colorRef{};
        colorRef.attachment = i;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorRefs.push_back(colorRef);
    }

    VkSubpassDescription subpassDesc{};
    subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDesc.colorAttachmentCount = m_colorAttachmentCount;
    subpassDesc.pColorAttachments = colorRefs.data();
    subpassDesc.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = 0u;
    dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    std::vector<VkAttachmentDescription> attachments;
    attachments.reserve(1u + m_colorAttachmentCount);
    attachments.push_back(depthAttachment);
    attachments.insert(attachments.end(), colorAttachments.begin(), colorAttachments.end());

    VkRenderPassCreateInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    renderPassInfo.attachmentCount = attachments.size();
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1u;
    renderPassInfo.pSubpasses = &subpassDesc;
    renderPassInfo.dependencyCount = 1u;
    renderPassInfo.pDependencies = &dependency;

    VK_CHECK(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass), "failed to create GBuffer render pass!");
}

void GBufferPass::createPipelineLayout(const std::vector<uint32_t>& vertCode, const std::vector<uint32_t>& fragCode)
{
    const uint32_t* shaderBinaries[] = { vertCode.data(), fragCode.data() };
    const size_t shaderSizes[]{ vertCode.size(), fragCode.size() };
    const VkShaderStageFlags shaderStages[] = { VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT };

    m_pipelineLayout = std::make_unique<vk::PipelineLayout>(m_device, shaderBinaries, shaderSizes, shaderStages, 2u);
}

void GBufferPass::createPipeline(const std::vector<uint32_t>& vertCode, const std::vector<uint32_t>& fragCode)
{
    VkShaderModuleCreateInfo shaderInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    shaderInfo.codeSize = vertCode.size() * 4u;
    shaderInfo.pCode = vertCode.data();

    VkShaderModule vertModule;
    VK_CHECK(vkCreateShaderModule(m_device, &shaderInfo, nullptr, &vertModule), "failed to create GBuffer pass vertex shader module!");

    shaderInfo.codeSize = fragCode.size() * 4u;
    shaderInfo.pCode = fragCode.data();

    VkShaderModule fragModule;
    VK_CHECK(vkCreateShaderModule(m_device, &shaderInfo, nullptr, &fragModule), "failed to create GBuffer pass fragment shader module!");

    VkPipelineShaderStageCreateInfo vertStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";
    VkPipelineShaderStageCreateInfo fragStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";
    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage, fragStage };

    VkVertexInputBindingDescription vertexBindingPosition{};
    vertexBindingPosition.binding = 0u;
    vertexBindingPosition.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vertexBindingPosition.stride = 12u;
    VkVertexInputBindingDescription vertexBindingNormal{};
    vertexBindingNormal.binding = 1u;
    vertexBindingNormal.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vertexBindingNormal.stride = 12u;
    VkVertexInputBindingDescription vertexBindingTangent{};
    vertexBindingTangent.binding = 2u;
    vertexBindingTangent.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vertexBindingTangent.stride = 16u;
    VkVertexInputBindingDescription vertexBindingTexCoord{};
    vertexBindingTexCoord.binding = 3u;
    vertexBindingTexCoord.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    vertexBindingTexCoord.stride = 8u;
    VkVertexInputBindingDescription vertexBindings[] = { vertexBindingPosition, vertexBindingNormal, vertexBindingTangent, vertexBindingTexCoord };

    VkVertexInputAttributeDescription vertexAttributePosition{};
    vertexAttributePosition.location = 0u;
    vertexAttributePosition.binding = 0u;
    vertexAttributePosition.format = VK_FORMAT_R32G32B32_SFLOAT;
    VkVertexInputAttributeDescription vertexAttributeNormal{};
    vertexAttributeNormal.location = 1u;
    vertexAttributeNormal.binding = 1u;
    vertexAttributeNormal.format = VK_FORMAT_R32G32B32_SFLOAT;
    VkVertexInputAttributeDescription vertexAttributeTangent{};
    vertexAttributeTangent.location = 2u;
    vertexAttributeTangent.binding = 2u;
    vertexAttributeTangent.format = VK_FORMAT_R32G32B32_SFLOAT;
    VkVertexInputAttributeDescription vertexAttributeTexCoord{};
    vertexAttributeTexCoord.location = 3u;
    vertexAttributeTexCoord.binding = 3u;
    vertexAttributeTexCoord.format = VK_FORMAT_R32G32_SFLOAT;
    VkVertexInputAttributeDescription vertexAttributes[] = { vertexAttributePosition, vertexAttributeNormal, vertexAttributeTangent, vertexAttributeTexCoord };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertexInputInfo.vertexBindingDescriptionCount = ARRAY_LENGTH(vertexBindings);
    vertexInputInfo.pVertexBindingDescriptions = vertexBindings;
    vertexInputInfo.vertexAttributeDescriptionCount = ARRAY_LENGTH(vertexAttributes);
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes;

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_extent.width);
    viewport.height = static_cast<float>(m_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor;
    scissor.offset = { 0, 0 };
    scissor.extent = m_extent;
    VkPipelineViewportStateCreateInfo viewportInfo{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportInfo.viewportCount = 1u;
    viewportInfo.pViewports = &viewport;
    viewportInfo.scissorCount = 1u;
    viewportInfo.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizerInfo{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizerInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizerInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizerInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizerInfo.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampleInfo{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachmentStates;
    colorBlendAttachmentStates.reserve(m_colorAttachmentCount);
    for (uint32_t i = 0; i < m_colorAttachmentCount; i++)
    {
        VkPipelineColorBlendAttachmentState state{};
        state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachmentStates.push_back(state);
    }

    VkPipelineColorBlendStateCreateInfo colorBlendInfo{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colorBlendInfo.attachmentCount = colorBlendAttachmentStates.size();
    colorBlendInfo.pAttachments = colorBlendAttachmentStates.data();

    VkPipelineDepthStencilStateCreateInfo depthInfo{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthInfo.depthTestEnable = VK_TRUE;
    depthInfo.depthWriteEnable = VK_TRUE;
    depthInfo.depthCompareOp = VK_COMPARE_OP_LESS;

    VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineInfo.stageCount = 2u;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
    pipelineInfo.pViewportState = &viewportInfo;
    pipelineInfo.pRasterizationState = &rasterizerInfo;
    pipelineInfo.pMultisampleState = &multisampleInfo;
    pipelineInfo.pDepthStencilState = &depthInfo;
    pipelineInfo.pColorBlendState = &colorBlendInfo;
    pipelineInfo.layout = *m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0u;

    VK_CHECK(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1u, &pipelineInfo, nullptr, &m_pipeline), "failed to create GBuffer pass pipeline!");

    vkDestroyShaderModule(m_device, fragModule, nullptr);
    vkDestroyShaderModule(m_device, vertModule, nullptr);
}

void GBufferPass::begin(VkCommandBuffer cmdBuf, VkFramebuffer framebuf) const
{
    VkRect2D scissor;
    scissor.offset = { 0, 0 };
    scissor.extent = m_extent;

    VkClearDepthStencilValue clearDepth{};
    clearDepth.depth = 1.0f;

    VkClearColorValue clearCol{};
    clearCol.float32[0] = 1.0f;
    clearCol.float32[1] = 1.0f;
    clearCol.float32[2] = 1.0f;
    clearCol.float32[3] = 1.0f;

    std::vector<VkClearValue> clearValues(1u + m_colorAttachmentCount);
    VkClearValue d;
    d.depthStencil = clearDepth;
    clearValues.push_back(d);
    for (uint32_t i = 0; i < m_colorAttachmentCount; i++)
    {
        VkClearValue c;
        c.color = clearCol;
        clearValues.push_back(c);
    }

    VkRenderPassBeginInfo beginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    beginInfo.renderPass = m_renderPass;
    beginInfo.framebuffer = framebuf;
    beginInfo.renderArea = scissor;
    beginInfo.clearValueCount = clearValues.size();
    beginInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmdBuf, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
}

void GBufferPass::end(VkCommandBuffer cmdBuf) const
{
    vkCmdEndRenderPass(cmdBuf);
}