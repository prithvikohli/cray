#include "renderer.h"

#include <fstream>

DrawableNode::DrawableNode(Node* node, VkDevice device, VkPipelineLayout layout, VkDescriptorSet descriptorSet, std::shared_ptr<vk::Buffer> uniformsBuffer, MaterialViews matViews, VkSampler sampler) : m_node(node), m_layout(layout), m_descriptorSet(descriptorSet), m_uniformsBuffer(uniformsBuffer), m_matViews(matViews)
{
    VkDescriptorBufferInfo uniformsInfo{};
    uniformsInfo.buffer = *m_uniformsBuffer;
    uniformsInfo.offset = 0u;
    uniformsInfo.range = m_uniformsBuffer->m_size;
    VkWriteDescriptorSet writeUniforms{};
    writeUniforms.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeUniforms.dstSet = m_descriptorSet;
    writeUniforms.dstBinding = 0u;
    writeUniforms.descriptorCount = 1u;
    writeUniforms.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writeUniforms.pBufferInfo = &uniformsInfo;

    VkDescriptorImageInfo albedoInfo{};
    albedoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    albedoInfo.imageView = *m_matViews.albedo;
    albedoInfo.sampler = sampler;
    VkWriteDescriptorSet writeAlbedo{};
    writeAlbedo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeAlbedo.dstSet = m_descriptorSet;
    writeAlbedo.dstBinding = 1u;
    writeAlbedo.descriptorCount = 1u;
    writeAlbedo.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeAlbedo.pImageInfo = &albedoInfo;

    VkDescriptorImageInfo metallicRoughnessInfo{};
    metallicRoughnessInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    metallicRoughnessInfo.imageView = *m_matViews.metallicRoughness;
    metallicRoughnessInfo.sampler = sampler;
    VkWriteDescriptorSet writeMetallicRoughness{};
    writeMetallicRoughness.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeMetallicRoughness.dstSet = m_descriptorSet;
    writeMetallicRoughness.dstBinding = 2u;
    writeMetallicRoughness.descriptorCount = 1u;
    writeMetallicRoughness.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeMetallicRoughness.pImageInfo = &metallicRoughnessInfo;

    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normalInfo.imageView = *m_matViews.normal;
    normalInfo.sampler = sampler;
    VkWriteDescriptorSet writeNormal{};
    writeNormal.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeNormal.dstSet = m_descriptorSet;
    writeNormal.dstBinding = 3u;
    writeNormal.descriptorCount = 1u;
    writeNormal.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeNormal.pImageInfo = &normalInfo;

    VkDescriptorImageInfo emissiveInfo{};
    emissiveInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    emissiveInfo.imageView = *m_matViews.emissive;
    emissiveInfo.sampler = sampler;
    VkWriteDescriptorSet writeEmissive{};
    writeEmissive.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeEmissive.dstSet = m_descriptorSet;
    writeEmissive.dstBinding = 4u;
    writeEmissive.descriptorCount = 1u;
    writeEmissive.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeEmissive.pImageInfo = &emissiveInfo;

    VkWriteDescriptorSet writes[] = { writeUniforms, writeAlbedo, writeMetallicRoughness, writeNormal, writeEmissive };
    vkUpdateDescriptorSets(device, ARRAY_LENGTH(writes), writes, 0u, nullptr);
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

void DrawableNode::draw(VkCommandBuffer cmd) const
{
    VkBuffer buffers[] = { m_node->mesh->positionBuffer->getHandle(), m_node->mesh->normalBuffer->getHandle(), m_node->mesh->texCoordBuffer->getHandle() };
    VkDeviceSize offsets[] = { 0u, 0u, 0u };
    vkCmdBindVertexBuffers(cmd, 0u, ARRAY_LENGTH(buffers), buffers, offsets);
    // TODO different index data types
    vkCmdBindIndexBuffer(cmd, *m_node->mesh->indexBuffer, 0u, VK_INDEX_TYPE_UINT16);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0u, 1u, &m_descriptorSet, 0u, nullptr);
    vkCmdDrawIndexed(cmd, m_node->mesh->indexCount, 1u, 0u, 0u, 0u);
}

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
    m_lightingUniforms = m_rc->createBuffer(sizeof(LightingUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    createSamplers();
    createLightingDescriptorSet();
    createSyncObjects();

    m_camera.pos = glm::vec3(0.0f);
    m_camera.view = glm::mat4(1.0f);
    m_camera.proj = glm::perspective(glm::radians(45.0f), m_rc->m_extent.width / (float)m_rc->m_extent.height, 0.1f, 100.0f);
    m_camera.proj[1][1] *= -1;
}

Renderer::~Renderer()
{
    vkDestroySampler(m_device, m_samplerNearest, nullptr);
    vkDestroySampler(m_device, m_samplerLinear, nullptr);
    if (m_descriptorPoolDrawables != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(m_device, m_descriptorPoolDrawables, nullptr);
    vkDestroyDescriptorPool(m_device, m_descriptorPoolLighting, nullptr);
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

void Renderer::createLightingDescriptorSet()
{
    // create descriptor set for lighting pass
    VkDescriptorPoolSize outputSize{};
    outputSize.descriptorCount = 1u;
    outputSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    VkDescriptorPoolSize inputSize{};
    inputSize.descriptorCount = 4u;
    inputSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    VkDescriptorPoolSize uniformsSize{};
    uniformsSize.descriptorCount = 1u;
    uniformsSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

    VkDescriptorPoolSize sizes[] = { outputSize, inputSize, uniformsSize };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1u;
    poolInfo.poolSizeCount = ARRAY_LENGTH(sizes);
    poolInfo.pPoolSizes = sizes;

    VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPoolLighting), "failed to create renderer lighting descriptor pool!");

    VkDescriptorSetLayout layout = m_lightingPass->getDescriptorSetLayout();
    VkDescriptorSetAllocateInfo setAllocInfo{};
    setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool = m_descriptorPoolLighting;
    setAllocInfo.descriptorSetCount = 1u;
    setAllocInfo.pSetLayouts = &layout;

    VK_CHECK(vkAllocateDescriptorSets(m_device, &setAllocInfo, &m_descriptorSetLighting), "failed to allocate renderer lighting descriptor set!");

    VkDescriptorImageInfo outputInfo{};
    outputInfo.imageView = *m_lightingView;
    outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet writeOutput{};
    writeOutput.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeOutput.dstSet = m_descriptorSetLighting;
    writeOutput.dstBinding = 0u;
    writeOutput.descriptorCount = 1u;
    writeOutput.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writeOutput.pImageInfo = &outputInfo;

    VkDescriptorImageInfo depthInfo{};
    depthInfo.imageView = *m_gbuffer.depth;
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthInfo.sampler = m_samplerNearest;
    VkWriteDescriptorSet writeDepth{};
    writeDepth.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDepth.dstSet = m_descriptorSetLighting;
    writeDepth.dstBinding = 1u;
    writeDepth.descriptorCount = 1u;
    writeDepth.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDepth.pImageInfo = &depthInfo;

    VkDescriptorImageInfo albedoMetallicInfo{};
    albedoMetallicInfo.imageView = *m_gbuffer.albedoMetallic;
    albedoMetallicInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    albedoMetallicInfo.sampler = m_samplerNearest;
    VkWriteDescriptorSet writeAlbedoMetallic{};
    writeAlbedoMetallic.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeAlbedoMetallic.dstSet = m_descriptorSetLighting;
    writeAlbedoMetallic.dstBinding = 2u;
    writeAlbedoMetallic.descriptorCount = 1u;
    writeAlbedoMetallic.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeAlbedoMetallic.pImageInfo = &albedoMetallicInfo;

    VkDescriptorImageInfo normalRoughnessInfo{};
    normalRoughnessInfo.imageView = *m_gbuffer.normalRoughness;
    normalRoughnessInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normalRoughnessInfo.sampler = m_samplerNearest;
    VkWriteDescriptorSet writeNormalRoughness{};
    writeNormalRoughness.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeNormalRoughness.dstSet = m_descriptorSetLighting;
    writeNormalRoughness.dstBinding = 3u;
    writeNormalRoughness.descriptorCount = 1u;
    writeNormalRoughness.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeNormalRoughness.pImageInfo = &normalRoughnessInfo;

    VkDescriptorBufferInfo uniformsInfo{};
    uniformsInfo.buffer = *m_lightingUniforms;
    uniformsInfo.offset = 0u;
    uniformsInfo.range = m_lightingUniforms->m_size;
    VkWriteDescriptorSet writeUniforms{};
    writeUniforms.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeUniforms.dstSet = m_descriptorSetLighting;
    writeUniforms.dstBinding = 4u;
    writeUniforms.descriptorCount = 1u;
    writeUniforms.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writeUniforms.pBufferInfo = &uniformsInfo;

    VkDescriptorImageInfo emissiveInfo{};
    emissiveInfo.imageView = *m_gbuffer.emissive;
    emissiveInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    emissiveInfo.sampler = m_samplerNearest;
    VkWriteDescriptorSet writeEmissive{};
    writeEmissive.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeEmissive.dstSet = m_descriptorSetLighting;
    writeEmissive.dstBinding = 5u;
    writeEmissive.descriptorCount = 1u;
    writeEmissive.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeEmissive.pImageInfo = &emissiveInfo;

    VkWriteDescriptorSet writes[] = { writeOutput, writeDepth, writeAlbedoMetallic, writeNormalRoughness, writeUniforms, writeEmissive };
    vkUpdateDescriptorSets(m_device, ARRAY_LENGTH(writes), writes, 0u, nullptr);
}

void Renderer::createSamplers()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
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

DrawableNode Renderer::createDrawableNode(Node* node) const
{
    // create uniforms buffer for this drawable node
    std::shared_ptr<vk::Buffer> uniformsBuffer = m_rc->createBuffer(sizeof(Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // create texture views
    VkImageSubresourceRange subRange{};
    subRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subRange.baseArrayLayer = 0u;
    subRange.baseMipLevel = 0u;
    subRange.layerCount = node->mesh->material->albedo->m_imageInfo.arrayLayers;
    subRange.baseMipLevel = 0u;
    subRange.levelCount = node->mesh->material->albedo->m_imageInfo.mipLevels;

    MaterialViews matViews;
    matViews.albedo = m_rc->createImageView(*node->mesh->material->albedo, VK_IMAGE_VIEW_TYPE_2D, subRange);
    matViews.metallicRoughness = m_rc->createImageView(*node->mesh->material->metallicRoughness, VK_IMAGE_VIEW_TYPE_2D, subRange);
    matViews.normal = m_rc->createImageView(*node->mesh->material->normal, VK_IMAGE_VIEW_TYPE_2D, subRange);
    matViews.emissive = m_rc->createImageView(*node->mesh->material->emissive, VK_IMAGE_VIEW_TYPE_2D, subRange);

    // allocate descriptor set
    VkDescriptorSetLayout layout = m_gbufferPass->getDescriptorSetLayout();
    VkDescriptorSetAllocateInfo setAllocInfo{};
    setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    setAllocInfo.descriptorPool = m_descriptorPoolDrawables;
    setAllocInfo.descriptorSetCount = 1u;
    setAllocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet;
    VK_CHECK(vkAllocateDescriptorSets(m_device, &setAllocInfo, &descriptorSet), "failed to allocate drawable node descriptor set!");

    return DrawableNode(node, m_device, m_gbufferPass->getPipelineLayout(), descriptorSet, uniformsBuffer, matViews, m_samplerLinear);
}

void Renderer::loadScene(const std::string& gltfFilename, bool binary)
{
    m_scene = std::make_unique<Scene>(m_rc, gltfFilename, binary);
    if (m_descriptorPoolDrawables != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(m_device, m_descriptorPoolDrawables, nullptr);

    // create descriptor pool for drawable nodes (for GBuffer pipeline)
    VkDescriptorPoolSize uniformsSize{};
    uniformsSize.descriptorCount = m_scene->m_nodes.size();
    uniformsSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    VkDescriptorPoolSize texturesSize{};
    texturesSize.descriptorCount = 4u * m_scene->m_nodes.size();
    texturesSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    VkDescriptorPoolSize poolSizes[] = { uniformsSize, texturesSize };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = m_scene->m_nodes.size();
    poolInfo.poolSizeCount = 2u;
    poolInfo.pPoolSizes = poolSizes;

    VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPoolDrawables), "failed to create renderer drawables descriptor pool!");

    for (Node* n : m_scene->m_nodes)
    {
        DrawableNode dn = createDrawableNode(n);
        m_drawableNodes.push_back(dn);
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

    // gbuffer pass
    m_gbufferPass->begin(m_cmdBuf);
    for (const DrawableNode& dn : m_drawableNodes)
    {
        dn.update(m_camera);
        dn.draw(m_cmdBuf);
    }
    m_gbufferPass->end(m_cmdBuf);

    // lighting pass
    m_lightingPass->bindPipeline(m_cmdBuf);

    // update lighting uniforms
    LightingUniforms lu;
    lu.viewPos = m_camera.pos;
    lu.invViewProj = glm::inverse(m_camera.proj * m_camera.view);
    lu.invRes = glm::vec2(1.0f / m_rc->m_extent.width, 1.0f / m_rc->m_extent.height);
    void* data = m_lightingUniforms->map();
    memcpy(data, &lu, sizeof(LightingUniforms));
    m_lightingUniforms->unmap();

    vkCmdBindDescriptorSets(m_cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_lightingPass->getPipelineLayout(), 0u, 1u, &m_descriptorSetLighting, 0u, nullptr);

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
