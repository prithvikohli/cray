#include "renderer.h"

#include <fstream>

Renderer::Renderer(vk::RenderContext* rc, const std::string& shadersDir) : m_rc(rc), m_device(rc->getDevice()), m_cmdBuf(rc->getCommandBuffer()), m_swapchainImages(rc->getSwapchainImages()), m_swapchainViews(rc->getSwapchainImageViews()), m_extent(rc->m_extent)
{
    std::vector<uint32_t> lightingCode = readShader(shadersDir + "lighting.comp.spv");
    m_lightingPass = std::make_unique<LightingPass>(m_rc, lightingCode);

    createSyncObjects();
    createDescriptorSets();
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

void Renderer::createDescriptorSets()
{
    VkDescriptorPoolSize poolSize{};
    poolSize.descriptorCount = m_swapchainViews.size();
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = m_swapchainViews.size();
    poolInfo.poolSizeCount = 1u;
    poolInfo.pPoolSizes = &poolSize;

    VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool), "failed to create renderer descriptor pool!");

    for (VkImageView view : m_swapchainViews)
    {
        VkDescriptorSetLayout layout = m_lightingPass->getDescriptorSetLayout();

        VkDescriptorSetAllocateInfo setAllocInfo{};
        setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        setAllocInfo.descriptorPool = m_descriptorPool;
        setAllocInfo.descriptorSetCount = 1u;
        setAllocInfo.pSetLayouts = &layout;
        VkDescriptorSet set;
        VK_CHECK(vkAllocateDescriptorSets(m_device, &setAllocInfo, &set), "failed to allocate renderer descriptor set!");

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageView = view;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkWriteDescriptorSet writeSet{};
        writeSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeSet.dstSet = set;
        writeSet.dstBinding = 0u;
        writeSet.descriptorCount = 1u;
        writeSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writeSet.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(m_device, 1u, &writeSet, 0u, nullptr);

        m_descriptorSets.push_back(set);
    }
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

    m_lightingPass->bindPipeline(m_cmdBuf);
    vkCmdBindDescriptorSets(m_cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_lightingPass->getPipelineLayout(), 0u, 1u, &m_descriptorSets[swapIdx], 0u, nullptr);

    VkImageMemoryBarrier imageMemoryBarrier{};
    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageMemoryBarrier.srcAccessMask = 0u;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageMemoryBarrier.image = m_swapchainImages[swapIdx];
    vkCmdPipelineBarrier(m_cmdBuf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &imageMemoryBarrier);

    vkCmdDispatch(m_cmdBuf, (m_extent.width + 7u) / 8u, (m_extent.height + 7u) / 8u, 1u);
    VK_CHECK(vkEndCommandBuffer(m_cmdBuf), "renderer failed to end command buffer!");

    VkPipelineStageFlags waitDstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
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
