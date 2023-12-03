#include "renderer.h"
#include "stb_image.h"

#include <fstream>

#include <glm/gtc/matrix_transform.hpp>

DrawableNode::DrawableNode(Node* node, std::shared_ptr<vk::DescriptorSet> descriptorSet, std::shared_ptr<vk::Buffer> uniformsBuffer, VkSampler sampler) : m_node(node), m_descriptorSet(descriptorSet), m_uniformsBuffer(uniformsBuffer)
{
    m_descriptorSet->setUniformBuffer(0u, *m_uniformsBuffer, m_uniformsBuffer->m_size);
    m_descriptorSet->setCombinedImageSampler(1u, *m_node->mesh->material->albedo, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, sampler);
    m_descriptorSet->setCombinedImageSampler(2u, *m_node->mesh->material->metallicRoughness, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, sampler);
    m_descriptorSet->setCombinedImageSampler(3u, *m_node->mesh->material->normal, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, sampler);
    m_descriptorSet->setCombinedImageSampler(4u, *m_node->mesh->material->emissive, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, sampler);
}

void DrawableNode::update(Camera cam) const
{
    Uniforms uniforms;

    uniforms.model = m_node->recursiveTransform;
    uniforms.view = cam.view;
    uniforms.proj = cam.proj;

    void* data = m_uniformsBuffer->map();
    memcpy(data, &uniforms, sizeof(Uniforms));
    m_uniformsBuffer->unmap();
}

void DrawableNode::draw(VkCommandBuffer cmd, VkPipelineLayout layout) const
{
    VkBuffer buffers[] = { m_node->mesh->positionBuffer->getHandle(), m_node->mesh->normalBuffer->getHandle(), m_node->mesh->texCoordBuffer->getHandle() };
    VkDeviceSize offsets[] = { 0u, 0u, 0u };
    vkCmdBindVertexBuffers(cmd, 0u, ARRAY_LENGTH(buffers), buffers, offsets);
    // TODO different index data types
    vkCmdBindIndexBuffer(cmd, *m_node->mesh->indexBuffer, 0u, VK_INDEX_TYPE_UINT16);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0u, 1u, &m_descriptorSet->m_handle, 0u, nullptr);
    vkCmdDrawIndexed(cmd, m_node->mesh->indexCount, 1u, 0u, 0u, 0u);
}

Renderer::Renderer(vk::RenderContext* rc, const std::string& shadersDir) : m_rc(rc), m_device(rc->getDevice()), m_shadersDir(shadersDir), m_cmdBuf(rc->getCommandBuffer())
{
    createSamplers();
    createGBuffer();
    setupLightingPass();
    createSyncObjects();
}

Renderer::~Renderer()
{
    vkDestroySampler(m_device, m_samplerNearest, nullptr);
    vkDestroySampler(m_device, m_samplerLinear, nullptr);
    vkDestroySemaphore(m_device, m_imageAcquiredSemaphore, nullptr);
    vkDestroySemaphore(m_device, m_renderFinishedSemaphore, nullptr);
    vkDestroyFence(m_device, m_inFlightFence, nullptr);
    vkDestroyFramebuffer(m_device, m_framebuf, nullptr);
}

void Renderer::createSamplers()
{
    VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

    VK_CHECK(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_samplerNearest), "failed to create sampler!");

    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    VK_CHECK(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_samplerLinear), "failed to create sampler!");
}

void Renderer::createGBuffer()
{

    // create GBuffer images
    VkExtent3D extent = { m_rc->m_extent.width, m_rc->m_extent.height, 1u };
    m_depthImg = m_rc->createImage(VK_FORMAT_D32_SFLOAT, extent, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
    m_albedoMetallicImg = m_rc->createImage(VK_FORMAT_R32G32B32A32_SFLOAT, extent, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
    m_normalRoughnessImg = m_rc->createImage(VK_FORMAT_R32G32B32A32_SFLOAT, extent, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
    m_emissiveImg = m_rc->createImage(VK_FORMAT_R32G32B32A32_SFLOAT, extent, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);

    // create GBuffer view bundle
    m_gbuffer.depth = m_rc->createImageView(*m_depthImg, VK_IMAGE_ASPECT_DEPTH_BIT);
    m_gbuffer.albedoMetallic = m_rc->createImageView(*m_albedoMetallicImg, VK_IMAGE_ASPECT_COLOR_BIT);
    m_gbuffer.normalRoughness = m_rc->createImageView(*m_normalRoughnessImg, VK_IMAGE_ASPECT_COLOR_BIT);
    m_gbuffer.emissive = m_rc->createImageView(*m_emissiveImg, VK_IMAGE_ASPECT_COLOR_BIT);

    // create GBuffer pass
    // TODO simplify making sure order of attachments is consistent
    std::vector<const vk::Image*> colorAttachments = { m_albedoMetallicImg.get(), m_normalRoughnessImg.get(), m_emissiveImg.get() };
    std::vector<uint32_t> vertCode = readShader("gbuffer.vert.spv");
    std::vector<uint32_t> fragCode = readShader("gbuffer.frag.spv");
    m_gbufferPass = std::make_unique<GBufferPass>(m_rc, m_depthImg.get(), colorAttachments, vertCode, fragCode);

    // create framebuffer
    VkImageView attachments[] = { *m_gbuffer.depth, *m_gbuffer.albedoMetallic, *m_gbuffer.normalRoughness, *m_gbuffer.emissive };
    VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    framebufferInfo.renderPass = *m_gbufferPass;
    framebufferInfo.attachmentCount = ARRAY_LENGTH(attachments);
    framebufferInfo.pAttachments = attachments;
    framebufferInfo.width = m_rc->m_extent.width;
    framebufferInfo.height = m_rc->m_extent.height;
    framebufferInfo.layers = 1u;

    VK_CHECK(vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_framebuf), "failed to create renderer framebuffer!");
}

void Renderer::setupLightingPass()
{
    // create lighting pipeline
    std::vector<uint32_t> lightingCode = readShader("lighting.comp.spv");
    const uint32_t* shaderBinary = lightingCode.data();
    size_t shaderSize = lightingCode.size();
    VkShaderStageFlags shaderStage = VK_SHADER_STAGE_COMPUTE_BIT;
    m_lightingPipelineLayout = std::make_unique<vk::PipelineLayout>(m_device, &shaderBinary, &shaderSize, &shaderStage, 1u);

    VkShaderModuleCreateInfo shaderInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    shaderInfo.codeSize = shaderSize * 4u;
    shaderInfo.pCode = lightingCode.data();
    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(m_device, &shaderInfo, nullptr, &shaderModule), "failed to create lighting shader module!");

    VkPipelineShaderStageCreateInfo shaderStageInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = "main";
    VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = *m_lightingPipelineLayout;

    VK_CHECK(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1u, &pipelineInfo, nullptr, &m_lightingPipeline), "failed to create lighting pipeline!");
    vkDestroyShaderModule(m_device, shaderModule, nullptr);

    // create lighting image and view
    m_lightingImg = m_rc->createImage(VK_FORMAT_R32G32B32A32_SFLOAT, { m_rc->m_extent.width, m_rc->m_extent.height, 1u }, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
    m_lightingView = m_rc->createImageView(*m_lightingImg, VK_IMAGE_ASPECT_COLOR_BIT);

    // create lighting uniforms
    m_lightingUniforms = m_rc->createBuffer(sizeof(LightingUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // create descriptor pool and set for lighting pass
    m_lightingDescriptorPool = std::make_unique<vk::DescriptorPool>(m_device, 1u, m_lightingPipelineLayout->m_bindings.data(), m_lightingPipelineLayout->m_bindings.size());
    m_lightingDescriptorSet = m_lightingDescriptorPool->allocateDescriptorSet(*m_lightingPipelineLayout);

    m_lightingDescriptorSet->setImage(0u, *m_lightingView, VK_IMAGE_LAYOUT_GENERAL);
    m_lightingDescriptorSet->setCombinedImageSampler(1u, *m_gbuffer.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, m_samplerNearest);
    m_lightingDescriptorSet->setCombinedImageSampler(2u, *m_gbuffer.albedoMetallic, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_samplerNearest);
    m_lightingDescriptorSet->setCombinedImageSampler(3u, *m_gbuffer.normalRoughness, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_samplerNearest);
    m_lightingDescriptorSet->setUniformBuffer(4u, *m_lightingUniforms, m_lightingUniforms->m_size);
    m_lightingDescriptorSet->setCombinedImageSampler(5u, *m_gbuffer.emissive, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_samplerNearest);
}

void Renderer::createSyncObjects()
{
    VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VK_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAcquiredSemaphore), "failed to create renderer image acquired semaphore!");
    VK_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphore), "failed to create renderer render finished semaphore!");

    VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFence), "failed to create renderer in flight fence!");
}

DrawableNode Renderer::createDrawableNode(Node* node) const
{
    // TODO deduplicate descriptor sets?
    // create uniforms buffer for this drawable node
    std::shared_ptr<vk::Buffer> uniformsBuffer = m_rc->createBuffer(sizeof(Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // allocate descriptor set
    std::shared_ptr<vk::DescriptorSet> set = m_drawablesDescriptorPool->allocateDescriptorSet(*m_gbufferPass->m_pipelineLayout);

    return DrawableNode(node, set, uniformsBuffer, m_samplerLinear);
}

void Renderer::loadScene(const std::string& gltfFilename, bool binary, const std::string& envmapHdrFilename)
{
    m_scene = std::make_unique<Scene>(m_rc, gltfFilename, binary);
    m_drawablesDescriptorPool = std::make_unique<vk::DescriptorPool>(m_device, m_scene->m_nodes.size(), m_gbufferPass->m_pipelineLayout->m_bindings.data(), m_gbufferPass->m_pipelineLayout->m_bindings.size());

    for (Node* n : m_scene->m_nodes)
    {
        DrawableNode dn = createDrawableNode(n);
        m_drawableNodes.push_back(dn);
    }

    // TODO make configurable
    m_camera.pos = glm::vec3(-3.0f, 1.0f, 0.0f);
    m_camera.view = glm::lookAt(m_camera.pos, glm::vec3(0.0f, 0.0, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    m_camera.proj = glm::perspective(glm::radians(45.0f), m_rc->m_extent.width / (float)m_rc->m_extent.height, 0.1f, 100.0f);
    m_camera.proj[1][1] *= -1;

    // update lighting uniforms
    {
        LightingUniforms lu;
        lu.viewPos = m_camera.pos;
        lu.invViewProj = glm::inverse(m_camera.proj * m_camera.view);
        lu.invRes = glm::vec2(1.0f / m_rc->m_extent.width, 1.0f / m_rc->m_extent.height);
        void* data = m_lightingUniforms->map();
        memcpy(data, &lu, sizeof(LightingUniforms));
        m_lightingUniforms->unmap();
    }

    // load environment map
    {
        int width, height, channels;
        float* imgData = stbi_loadf(envmapHdrFilename.c_str(), &width, &height, &channels, 4);
        if (imgData == nullptr)
            throw std::runtime_error("failed to load environment map!");

        VkExtent3D extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1u };
        std::shared_ptr<vk::Image> stagingImg = m_rc->createImage(VK_FORMAT_R32G32B32A32_SFLOAT, extent, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_TILING_LINEAR);
        void* data = stagingImg->map();
        memcpy(data, imgData, width * height * 16u);
        stagingImg->unmap();
        stbi_image_free(imgData);

        m_envMapImg = m_rc->createImage(VK_FORMAT_R32G32B32A32_SFLOAT, extent, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
        m_rc->copyStagingImage(*m_envMapImg, *stagingImg, extent, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        m_envMapView = m_rc->createImageView(*m_envMapImg, VK_IMAGE_ASPECT_COLOR_BIT);
        m_lightingDescriptorSet->setCombinedImageSampler(6u, *m_envMapView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_samplerNearest);
    }

    // build AS
    m_AS = std::make_unique<vk::AccelerationStructure>(m_rc, *m_scene);
    m_lightingDescriptorSet->setAccelerationStructure(7u, m_AS->getTlas());
}

void Renderer::render() const
{
    VK_CHECK(vkWaitForFences(m_device, 1u, &m_inFlightFence, VK_TRUE, UINT64_MAX), "renderer failed to wait for in flight fence!");
    VK_CHECK(vkResetFences(m_device, 1u, &m_inFlightFence), "renderer failed to reset in flight fence!");

    uint32_t swapIdx = m_rc->acquireNextSwapchainImage(m_imageAcquiredSemaphore);
    VkImage swapImg = m_rc->getSwapchainImage(swapIdx);

    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(m_cmdBuf, &beginInfo), "renderer failed to begin command buffer!");

    // gbuffer pass
    m_gbufferPass->begin(m_cmdBuf, m_framebuf);
    for (const DrawableNode& dn : m_drawableNodes)
    {
        dn.update(m_camera);
        dn.draw(m_cmdBuf, *m_gbufferPass->m_pipelineLayout);
    }
    m_gbufferPass->end(m_cmdBuf);

    // lighting pass
    vkCmdBindPipeline(m_cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_lightingPipeline);
    vkCmdBindDescriptorSets(m_cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, *m_lightingPipelineLayout, 0u, 1u, &m_lightingDescriptorSet->m_handle, 0u, nullptr);

    m_cmdBuf.imageMemoryBarrier(*m_lightingView, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0u, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);

    vkCmdDispatch(m_cmdBuf, (m_rc->m_extent.width + 7u) / 8u, (m_rc->m_extent.height + 7u) / 8u, 1u);

    VkImageSubresourceRange subRange{};
    subRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subRange.baseArrayLayer = 0u;
    subRange.baseMipLevel = 0u;
    subRange.layerCount = 1u;
    subRange.levelCount = 1u;
    VkImageMemoryBarrier imageMemoryBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    imageMemoryBarrier.srcAccessMask = 0u;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    imageMemoryBarrier.image = swapImg;
    imageMemoryBarrier.subresourceRange = subRange;

    vkCmdPipelineBarrier(m_cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, 0u, nullptr, 0u, nullptr, 1u, &imageMemoryBarrier);
    m_cmdBuf.imageMemoryBarrier(*m_lightingView, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

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

    m_rc->submitToQueue(m_imageAcquiredSemaphore, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, m_renderFinishedSemaphore, m_inFlightFence);
    m_rc->present(swapIdx, m_renderFinishedSemaphore);
}

std::vector<uint32_t> Renderer::readShader(const std::string& filename) const
{
    std::ifstream file(m_shadersDir + filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("failed to open file \"" + m_shadersDir + filename + "\"!");
    size_t fileSize = file.tellg();
    std::vector<uint32_t> buffer((fileSize + 3u) / 4u);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();
    return buffer;
}
