#include "renderer.h"

#include <fstream>

Renderer::Renderer(vk::RenderContext* rc, const std::string& shadersDir) : m_rc(rc), m_device(rc->getDevice()), m_cmdBuf(rc->getCommandBuffer())
{
    // create GBuffer pass
    std::vector<uint32_t> vertCode = readShader(shadersDir + "gbuffer.vert.spv");
    std::vector<uint32_t> fragCode = readShader(shadersDir + "gbuffer.frag.spv");
    m_gbufferPass = std::make_unique<GBufferPass>(m_rc, vertCode, fragCode);

    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_D32_SFLOAT;
        imageInfo.extent = { m_rc->m_extent.width, m_rc->m_extent.height, 1u };
        imageInfo.mipLevels = 1u;
        imageInfo.arrayLayers = 1u;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        m_depthImg = m_rc->createImage(imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);

        imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        m_albedoMetallicImg = m_rc->createImage(imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
        m_normalRoughnessImg = m_rc->createImage(imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
        m_emissiveImg = m_rc->createImage(imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);

        VkImageSubresourceRange subRange{};
        subRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        subRange.baseArrayLayer = 0u;
        subRange.baseMipLevel = 0u;
        subRange.layerCount = 1u;
        subRange.levelCount = 1u;

        m_gbuffer.depth = m_rc->createImageView(*m_depthImg, VK_IMAGE_VIEW_TYPE_2D, subRange);

        subRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        m_gbuffer.albedoMetallic = m_rc->createImageView(*m_albedoMetallicImg, VK_IMAGE_VIEW_TYPE_2D, subRange);
        m_gbuffer.normalRoughness = m_rc->createImageView(*m_normalRoughnessImg, VK_IMAGE_VIEW_TYPE_2D, subRange);
        m_gbuffer.emissive = m_rc->createImageView(*m_emissiveImg, VK_IMAGE_VIEW_TYPE_2D, subRange);
    }

    m_gbufferPass->bindBundle(m_gbuffer);

    // create lighting pass
    std::vector<uint32_t> lightingCode = readShader(shadersDir + "lighting.comp.spv");
    m_lightingPass = std::make_unique<LightingPass>(m_rc, lightingCode);

    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        imageInfo.extent = { m_rc->m_extent.width, m_rc->m_extent.height, 1u };
        imageInfo.mipLevels = 1u;
        imageInfo.arrayLayers = 1u;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        m_lightingImg = m_rc->createImage(imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);

        VkImageSubresourceRange subRange{};
        subRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subRange.baseArrayLayer = 0u;
        subRange.baseMipLevel = 0u;
        subRange.layerCount = 1u;
        subRange.levelCount = 1u;

        m_lightingView = m_rc->createImageView(*m_lightingImg, VK_IMAGE_VIEW_TYPE_2D, subRange);
    }

    // create other objects owned by renderer
    createSyncObjects();
    createDescriptorSet();
}

Renderer::~Renderer()
{
    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    vkDestroySemaphore(m_device, m_imageAcquiredSemaphore, nullptr);
    vkDestroySemaphore(m_device, m_renderFinishedSemaphore, nullptr);
    vkDestroyFence(m_device, m_inFlightFence, nullptr);
}

void Renderer::createSyncObjects()
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAcquiredSemaphore), "failed to create renderer image acquired semaphore!");
    VK_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphore), "failed to create renderer render finished semaphore!");

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFence), "failed to create renderer in flight fence!");
}

void Renderer::createDescriptorSet()
{
    // create descriptor set for lighting pass
    VkDescriptorPoolSize poolSize{};
    poolSize.descriptorCount = 1u;
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1u;
    poolInfo.poolSizeCount = 1u;
    poolInfo.pPoolSizes = &poolSize;

    VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool), "failed to create renderer descriptor pool!");

    VkDescriptorSetLayout layout = m_lightingPass->getDescriptorSetLayout();
    VkDescriptorSetAllocateInfo setAllocInfo{};
    setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool = m_descriptorPool;
    setAllocInfo.descriptorSetCount = 1u;
    setAllocInfo.pSetLayouts = &layout;

    VK_CHECK(vkAllocateDescriptorSets(m_device, &setAllocInfo, &m_descriptorSet), "failed to allocate renderer descriptor set!");

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageView = *m_lightingView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet writeSet{};
    writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeSet.dstSet = m_descriptorSet;
    writeSet.dstBinding = 0u;
    writeSet.descriptorCount = 1u;
    writeSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writeSet.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(m_device, 1u, &writeSet, 0u, nullptr);
}

void Renderer::loadScene(const std::string& gltfBinaryFilename)
{
    m_scene = std::make_unique<Scene>(m_rc, gltfBinaryFilename);
}

void Renderer::render() const
{
    VK_CHECK(vkWaitForFences(m_device, 1u, &m_inFlightFence, VK_TRUE, UINT64_MAX), "renderer failed to wait for in flight fence!");
    VK_CHECK(vkResetFences(m_device, 1u, &m_inFlightFence), "renderer failed to reset in flight fence!");

    uint32_t swapIdx;
    m_rc->acquireNextSwapchainImage(&swapIdx, m_imageAcquiredSemaphore);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(m_cmdBuf, &beginInfo), "renderer failed to begin command buffer!");

    // gbuffer pass
    m_gbufferPass->begin(m_cmdBuf);
    m_gbufferPass->end(m_cmdBuf);

    // lighting pass
    m_lightingPass->bindPipeline(m_cmdBuf);
    vkCmdBindDescriptorSets(m_cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_lightingPass->getPipelineLayout(), 0u, 1u, &m_descriptorSet, 0u, nullptr);

    VkImageSubresourceRange subRange{};
    subRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subRange.baseArrayLayer = 0u;
    subRange.baseMipLevel = 0u;
    subRange.layerCount = 1u;
    subRange.levelCount = 1u;
    VkImageMemoryBarrier imageMemoryBarrier{};
    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageMemoryBarrier.srcAccessMask = 0u;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageMemoryBarrier.image = *m_lightingImg;
    imageMemoryBarrier.subresourceRange = subRange;

    vkCmdPipelineBarrier(m_cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &imageMemoryBarrier);
    vkCmdDispatch(m_cmdBuf, (m_rc->m_extent.width + 7u) / 8u, (m_rc->m_extent.height + 7u) / 8u, 1u);

    imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    vkCmdPipelineBarrier(m_cmdBuf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &imageMemoryBarrier);

    VkImage swapImg = m_rc->getSwapchainImage(swapIdx);
    imageMemoryBarrier.srcAccessMask = 0u;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    imageMemoryBarrier.image = swapImg;

    vkCmdPipelineBarrier(m_cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &imageMemoryBarrier);

    VkImageSubresourceLayers subLayers{};
    subLayers.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subLayers.baseArrayLayer = 0u;
    subLayers.layerCount = 1u;
    subLayers.mipLevel = 0u;
    VkOffset3D offsets[] = { {0, 0, 0}, {static_cast<int32_t>(m_rc->m_extent.width), static_cast<int32_t>(m_rc->m_extent.height), 1} };
    VkImageBlit blit{};
    blit.srcSubresource = subLayers;
    blit.dstSubresource = subLayers;
    blit.srcOffsets[0] = offsets[0];
    blit.dstOffsets[0] = offsets[0];
    blit.srcOffsets[1] = offsets[1];
    blit.dstOffsets[1] = offsets[1];

    vkCmdBlitImage(m_cmdBuf, *m_lightingImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &blit, VK_FILTER_NEAREST);

    imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    imageMemoryBarrier.dstAccessMask = 0u;
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    
    vkCmdPipelineBarrier(m_cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &imageMemoryBarrier);

    VK_CHECK(vkEndCommandBuffer(m_cmdBuf), "renderer failed to end command buffer!");

    VkPipelineStageFlags waitDstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1u;
    submitInfo.pWaitSemaphores = &m_imageAcquiredSemaphore;
    submitInfo.pWaitDstStageMask = &waitDstStage;
    submitInfo.commandBufferCount = 1u;
    submitInfo.pCommandBuffers = &m_cmdBuf;
    submitInfo.signalSemaphoreCount = 1u;
    submitInfo.pSignalSemaphores = &m_renderFinishedSemaphore;

    m_rc->submitToQueue(submitInfo, m_inFlightFence);
    m_rc->present(swapIdx, m_renderFinishedSemaphore);
}

std::vector<uint32_t> Renderer::readShader(const std::string& filename) const
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("failed to open file \"" + filename + "\"!");
    size_t fileSize = file.tellg();
    std::vector<uint32_t> buffer((fileSize + 3u) / 4u);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();
    return buffer;
}
