#include "renderer.h"

Renderer::~Renderer()
{
    if (m_cmdPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(*m_gpu, m_cmdPool, nullptr);
    if (!m_swapchainViews.empty())
    {
        for (VkImageView view : m_swapchainViews)
            vkDestroyImageView(*m_gpu, view, nullptr);
    }
    vkDestroySemaphore(*m_gpu, m_imageAcquired, nullptr);
    vkDestroySemaphore(*m_gpu, m_renderDone, nullptr);
    vkDestroyFence(*m_gpu, m_renderFence, nullptr);
}

bool Renderer::init()
{
    // create swapchain
    m_swapchain = std::make_unique<vk::Swapchain>(m_gpu);
    if (!m_swapchain->create(m_window, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        return false;

    for (VkImage img : m_swapchain->m_images)
    {
        VkImageSubresourceRange subRange{};
        subRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subRange.baseMipLevel = 0u;
        subRange.levelCount = 1u;
        subRange.baseArrayLayer = 0u;
        subRange.layerCount = 1u;

        VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = img;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_swapchain->m_createInfo.imageFormat;
        viewInfo.subresourceRange = subRange;

        VkImageView view;
        VkResult res = vkCreateImageView(*m_gpu, &viewInfo, nullptr, &view);
        if (res != VK_SUCCESS)
            return false;
        m_swapchainViews.push_back(view);
    }

    // create shaders
    vk::Shader vertShader(m_gpu);
    if (!vertShader.create("src/shaders/test.vert.spv", VK_SHADER_STAGE_VERTEX_BIT))
        return false;

    vk::Shader fragShader(m_gpu);
    if (!fragShader.create("src/shaders/test.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT))
        return false;

    std::unordered_set<vk::Shader*> shaderGroup = { &vertShader, &fragShader };

    // create graphics pipeline
    m_gfxPipe = std::make_unique<vk::GraphicsPipeline>(m_gpu);
    //m_gfxPipe->m_vertexBindings.push_back({ 0u, 12u, VK_VERTEX_INPUT_RATE_VERTEX });
    //m_gfxPipe->m_vertexBindings.push_back({ 1u, 12u, VK_VERTEX_INPUT_RATE_VERTEX });
    //m_gfxPipe->m_vertexBindings.push_back({ 2u, 16u, VK_VERTEX_INPUT_RATE_VERTEX });
    //m_gfxPipe->m_vertexBindings.push_back({ 3u, 8u, VK_VERTEX_INPUT_RATE_VERTEX });

    //m_gfxPipe->m_vertexAttributes.push_back({ 0u, 0u, VK_FORMAT_R32G32B32_SFLOAT });
    //m_gfxPipe->m_vertexAttributes.push_back({ 1u, 1u, VK_FORMAT_R32G32B32_SFLOAT });
    //m_gfxPipe->m_vertexAttributes.push_back({ 2u, 2u, VK_FORMAT_R32G32B32A32_SFLOAT });
    //m_gfxPipe->m_vertexAttributes.push_back({ 3u, 3u, VK_FORMAT_R32G32_SFLOAT });

    VkPipelineColorBlendAttachmentState colorBlendAttachmentState{};
    colorBlendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    m_gfxPipe->m_colorBlendAttachmentStates.push_back(colorBlendAttachmentState);

    m_gfxPipe->m_rasterizerInfo.cullMode = VK_CULL_MODE_NONE;
    if (!m_gfxPipe->create(shaderGroup, m_width, m_height))
        return false;

    // get GCT queue
    m_gct = m_gpu->getQueue(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT, 0u);

    // create cmd pool
    VkCommandPoolCreateInfo cmdPoolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    cmdPoolInfo.queueFamilyIndex = m_gpu->m_queueFlagsToQueueFamily.at(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT);
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkResult res = vkCreateCommandPool(*m_gpu, &cmdPoolInfo, nullptr, &m_cmdPool);
    if (res != VK_SUCCESS)
        return false;

    // allocate cmd buffer
    m_cmdBuf = std::make_unique<vk::CommandBuffer>(m_gpu, m_cmdPool);
    if (!m_cmdBuf->create())
        return false;

    // create sync objects
    VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    res = vkCreateSemaphore(*m_gpu, &semaphoreInfo, nullptr, &m_imageAcquired);
    if (res != VK_SUCCESS)
        return false;
    res = vkCreateSemaphore(*m_gpu, &semaphoreInfo, nullptr, &m_renderDone);
    if (res != VK_SUCCESS)
        return false;

    VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    res = vkCreateFence(*m_gpu, &fenceInfo, nullptr, &m_renderFence);
    if (res != VK_SUCCESS)
        return false;

    return true;
}

bool Renderer::render()
{
    vkWaitForFences(*m_gpu, 1u, &m_renderFence, VK_TRUE, UINT64_MAX);
    vkResetFences(*m_gpu, 1u, &m_renderFence);
    uint32_t swapIdx;
    m_swapchain->acquireNextImage(&swapIdx, m_imageAcquired);

    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkBeginCommandBuffer(*m_cmdBuf, &beginInfo);

    m_cmdBuf->bindGraphicsPipeline(m_gfxPipe.get());
    m_cmdBuf->imageMemoryBarrier(m_swapchain->m_images[swapIdx], VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

    VkRect2D renderArea{};
    renderArea.extent = { m_width, m_height };
    VkClearColorValue clearCol;
    clearCol.float32[0] = 0.0f;
    clearCol.float32[1] = 0.0f;
    clearCol.float32[2] = 0.0f;
    clearCol.float32[3] = 1.0f;
    VkClearValue clearValue;
    clearValue.color = clearCol;
    VkRenderingAttachmentInfo attachmentInfo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    attachmentInfo.imageView = m_swapchainViews[swapIdx];
    attachmentInfo.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    attachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachmentInfo.clearValue = clearValue;
    VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    renderingInfo.renderArea = renderArea;
    renderingInfo.layerCount = 1u;
    renderingInfo.colorAttachmentCount = 1u;
    renderingInfo.pColorAttachments = &attachmentInfo;

    vkCmdBeginRendering(*m_cmdBuf, &renderingInfo);
    vkCmdDraw(*m_cmdBuf, 3u, 1u, 0u, 0u);
    vkCmdEndRendering(*m_cmdBuf);

    m_cmdBuf->imageMemoryBarrier(m_swapchain->m_images[swapIdx], VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0u, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0u, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    vkEndCommandBuffer(*m_cmdBuf);

    m_gpu->submitToQueue(m_gct, *m_cmdBuf, m_imageAcquired, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, m_renderDone, m_renderFence);

    m_swapchain->present(m_gct, swapIdx, m_renderDone);

    return true;
}
