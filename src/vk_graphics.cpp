#include "vk_graphics.h"

#include <spirv_cross/spirv_glsl.hpp>
#include <fstream>

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

namespace vk
{

Instance::Instance()
{
    m_appInfo.apiVersion = VK_API_VERSION_1_3;
    m_createInfo.pApplicationInfo = &m_appInfo;
}

Instance::~Instance()
{
    if (m_handle != VK_NULL_HANDLE)
        destroy();
}

bool Instance::create()
{
    if (m_handle != VK_NULL_HANDLE)
        return false;

    if (!m_enabledLayers.empty())
    {
        uint32_t availableLayerCount;
        vkEnumerateInstanceLayerProperties(&availableLayerCount, nullptr);
        std::vector<VkLayerProperties> availableLayerProps(availableLayerCount);
        vkEnumerateInstanceLayerProperties(&availableLayerCount, availableLayerProps.data());

        // check all requested layers are available
        for (const char* layer : m_enabledLayers)
        {
            bool available = false;
            for (VkLayerProperties& props : availableLayerProps)
            {
                if (strcmp(props.layerName, layer) == 0)
                {
                    available = true;
                    break;
                }
            }
            if (!available)
            {
                LOGE("Requested instance layer " + std::string(layer) + " not available.");
                return false;
            }
        }

        m_createInfo.enabledLayerCount = m_enabledLayers.size();
        m_createInfo.ppEnabledLayerNames = m_enabledLayers.data();
    }
    if (!m_enabledExtensions.empty())
    {
        uint32_t availableExtensionCount;
        vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensionProps(availableExtensionCount);
        vkEnumerateInstanceExtensionProperties(nullptr, &availableExtensionCount, availableExtensionProps.data());

        // check all requested extensions are available
        for (const char* ext : m_enabledExtensions)
        {
            bool available = false;
            for (VkExtensionProperties& props : availableExtensionProps)
            {
                if (strcmp(props.extensionName, ext) == 0)
                {
                    available = true;
                    break;
                }
            }
            if (!available)
            {
                LOGE("Requested instance extension " + std::string(ext) + "not available.");
                return false;
            }
        }

        m_createInfo.enabledExtensionCount = m_enabledExtensions.size();
        m_createInfo.ppEnabledExtensionNames = m_enabledExtensions.data();
    }

    VkResult res = vkCreateInstance(&m_createInfo, nullptr, &m_handle);
    return res == VK_SUCCESS;
}

Device::Device(Instance* instance) : m_instance(instance)
{
    m_createInfo.pNext = &m_enabledFeatures;
}

Device::~Device()
{
    if (m_handle != VK_NULL_HANDLE)
        destroy();
}

bool Device::create()
{
    if (m_handle != VK_NULL_HANDLE)
        return false;

    uint32_t physicalDeviceCount;
    vkEnumeratePhysicalDevices(*m_instance, &physicalDeviceCount, nullptr);
    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    vkEnumeratePhysicalDevices(*m_instance, &physicalDeviceCount, physicalDevices.data());

    // choose physical device
    for (VkPhysicalDevice pd : physicalDevices)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pd, &props);
        if (props.deviceType != m_physicalDeviceType)
            continue;

        uint32_t supportedExtensionsCount;
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &supportedExtensionsCount, nullptr);
        std::vector<VkExtensionProperties> supportedExtensions(supportedExtensionsCount);
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &supportedExtensionsCount, supportedExtensions.data());

        bool allSupported = true;
        for (const char* ext : m_enabledExtensions)
        {
            bool supported = false;
            for (VkExtensionProperties& ep : supportedExtensions)
            {
                if (strcmp(ext, ep.extensionName) == 0)
                {
                    supported = true;
                    break;
                }
            }
            if (!supported)
            {
                LOGW("Requested device extension \'" + std::string(ext) + "\' not available on physical device <" + std::string(props.deviceName) + ">.\nTrying next physical device...");
                allSupported = false;
                break;
            }
        }
        if (!allSupported)
            continue;

        m_physicalDevice = pd;
        break;
    }

    if (m_physicalDevice == VK_NULL_HANDLE)
    {
        LOGE("Failed to find suitable physical device.");
        return false;
    }

    // setup queue create infos
    uint32_t queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilyProps(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilyProps.data());

    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    uint32_t maxCount = 0u;
    for (const QueueRequirements& qr : m_queueRequirements)
        maxCount = std::max(maxCount, qr.count);
    std::vector<float> queuePriorities(maxCount, 0.0f);

    for (const QueueRequirements& qr : m_queueRequirements)
    {
        bool queueFound = false;
        for (uint32_t i = 0; i < queueFamilyProps.size(); i++)
        {
            if ((queueFamilyProps[i].queueFlags & qr.flags) == qr.flags)
            {
                VkDeviceQueueCreateInfo qi{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
                qi.queueFamilyIndex = i;
                qi.queueCount = qr.count;
                qi.pQueuePriorities = queuePriorities.data();

                m_queueFlagsToQueueFamily[qr.flags] = i;

                queueInfos.push_back(qi);
                queueFound = true;
                break;
            }
        }
        if (!queueFound)
        {
            LOGE("Failed to satisfy all queue requirements.");
            return false;
        }
    }

    if (!queueInfos.empty())
    {
        m_createInfo.queueCreateInfoCount = queueInfos.size();
        m_createInfo.pQueueCreateInfos = queueInfos.data();
    }
    if (!m_enabledExtensions.empty())
    {
        m_createInfo.enabledExtensionCount = m_enabledExtensions.size();
        m_createInfo.ppEnabledExtensionNames = m_enabledExtensions.data();
    }

    VkResult res = vkCreateDevice(m_physicalDevice, &m_createInfo, nullptr, &m_handle);
    return res == VK_SUCCESS;
}

VkQueue Device::getQueue(VkQueueFlags flags, uint32_t idx) const
{
    VkQueue queue;
    uint32_t qf = m_queueFlagsToQueueFamily.at(flags);
    vkGetDeviceQueue(m_handle, qf, idx, &queue);
    return queue;
}

bool Device::submitToQueue(VkQueue queue, VkCommandBuffer cmdBuf, VkSemaphore waitSemaphore, VkPipelineStageFlags waitStageMask, VkSemaphore signalSemaphore, VkFence fence) const
{
    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.waitSemaphoreCount = 1u;
    submitInfo.pWaitSemaphores = &waitSemaphore;
    submitInfo.pWaitDstStageMask = &waitStageMask;
    submitInfo.commandBufferCount = 1u;
    submitInfo.pCommandBuffers = &cmdBuf;
    submitInfo.signalSemaphoreCount = 1u;
    submitInfo.pSignalSemaphores = &signalSemaphore;

    VkResult res = vkQueueSubmit(queue, 1u, &submitInfo, fence);
    return res == VK_SUCCESS;
}

bool Device::waitIdle() const
{
    VkResult res = vkDeviceWaitIdle(m_handle);
    return res == VK_SUCCESS;
}

Swapchain::Swapchain(Device* device) : m_device(device)
{
    m_createInfo.imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
    m_createInfo.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    m_createInfo.imageArrayLayers = 1u;
    m_createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    m_createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    m_createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    m_createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    m_createInfo.clipped = VK_TRUE;
}

Swapchain::~Swapchain()
{
    if (m_handle != VK_NULL_HANDLE)
        destroy();
}

bool Swapchain::create(GLFWwindow* window, VkImageUsageFlags usage)
{
    if (m_handle != VK_NULL_HANDLE)
        return false;

    VkResult res = glfwCreateWindowSurface(*m_device->m_instance, window, nullptr, &m_surface);
    if (res != VK_SUCCESS)
        return false;

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device->m_physicalDevice, m_surface, &surfaceCapabilities);

    m_createInfo.surface = m_surface;
    m_createInfo.minImageCount = surfaceCapabilities.minImageCount + 1u;
    m_createInfo.imageExtent = surfaceCapabilities.currentExtent;
    m_createInfo.imageUsage = usage;

    res = vkCreateSwapchainKHR(*m_device, &m_createInfo, nullptr, &m_handle);
    if (res != VK_SUCCESS)
        return false;

    uint32_t imagesCount;
    vkGetSwapchainImagesKHR(*m_device, m_handle, &imagesCount, nullptr);
    m_images.resize(imagesCount);
    vkGetSwapchainImagesKHR(*m_device, m_handle, &imagesCount, m_images.data());

    return true;
}

void Swapchain::destroy()
{
    vkDestroySwapchainKHR(*m_device, m_handle, nullptr);
    vkDestroySurfaceKHR(*m_device->m_instance, m_surface, nullptr);
}

bool Swapchain::acquireNextImage(uint32_t* idx, VkSemaphore acquiredSemaphore) const
{
    VkResult res = vkAcquireNextImageKHR(*m_device, m_handle, UINT64_MAX, acquiredSemaphore, VK_NULL_HANDLE, idx);
    return res == VK_SUCCESS;
}

bool Swapchain::present(VkQueue queue, uint32_t idx, VkSemaphore waitSemaphore) const
{
    VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    presentInfo.waitSemaphoreCount = 1u;
    presentInfo.pWaitSemaphores = &waitSemaphore;
    presentInfo.swapchainCount = 1u;
    presentInfo.pSwapchains = &m_handle;
    presentInfo.pImageIndices = &idx;

    VkResult res = vkQueuePresentKHR(queue, &presentInfo);
    return res == VK_SUCCESS;
}

Buffer::Buffer(VmaAllocator allocator) : m_allocator(allocator)
{
    m_createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
}

Buffer::~Buffer()
{
    if (m_handle != VK_NULL_HANDLE)
        destroy();
}

bool Buffer::create(VkDeviceSize size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocationFlags, VkMemoryPropertyFlags memoryFlags, VkDeviceSize minAlignment)
{
    if (m_handle != VK_NULL_HANDLE)
        return false;

    m_createInfo.size = size;
    m_createInfo.usage = bufferUsage;

    m_allocationInfo.usage = memoryUsage;
    m_allocationInfo.flags = allocationFlags;
    m_allocationInfo.requiredFlags = memoryFlags;

    VkResult res;
    if (minAlignment != 0u)
    {
        res = vmaCreateBufferWithAlignment(m_allocator, &m_createInfo, &m_allocationInfo, minAlignment, &m_handle, &m_allocation, nullptr);
    }
    else
    {
        res = vmaCreateBuffer(m_allocator, &m_createInfo, &m_allocationInfo, &m_handle, &m_allocation, nullptr);
    }
    return res == VK_SUCCESS;
}

bool Buffer::map(void** data) const
{
    VkResult res = vmaMapMemory(m_allocator, m_allocation, data);
    return res == VK_SUCCESS;
}

Image::Image(VmaAllocator allocator) : m_allocator(allocator)
{
    m_createInfo.imageType = VK_IMAGE_TYPE_2D;
    m_createInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    m_createInfo.mipLevels = 1u;
    m_createInfo.arrayLayers = 1u;
    m_createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    m_createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
}

Image::~Image()
{
    if (m_handle != VK_NULL_HANDLE)
        destroy();
}

bool Image::create(VkExtent3D extent, VkImageTiling tiling, VkImageLayout initialLayout, VkImageUsageFlags imageUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocationFlags, VkMemoryPropertyFlags memoryFlags)
{
    if (m_handle != VK_NULL_HANDLE)
        return false;

    m_allocationInfo.usage = memoryUsage;
    m_allocationInfo.flags = allocationFlags;
    m_allocationInfo.requiredFlags = memoryFlags;

    m_createInfo.extent = extent;
    m_createInfo.tiling = tiling;
    m_createInfo.usage = imageUsage;
    m_createInfo.initialLayout = initialLayout;

    m_layout = initialLayout;

    VkResult res = vmaCreateImage(m_allocator, &m_createInfo, &m_allocationInfo, &m_handle, &m_allocation, nullptr);
    return res == VK_SUCCESS;
}

bool Image::map(void** data) const
{
    VkResult res = vmaMapMemory(m_allocator, m_allocation, data);
    return res == VK_SUCCESS;
}


ImageView::ImageView(Device* device, Image& img) : m_device(device), m_img(img)
{
    VkImageSubresourceRange subRange{};
    subRange.baseMipLevel = 0u;
    subRange.levelCount = m_img.m_createInfo.mipLevels;
    subRange.baseArrayLayer = 0u;
    subRange.layerCount = m_img.m_createInfo.arrayLayers;

    m_createInfo.image = img;
    m_createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    m_createInfo.format = img.m_createInfo.format;
    m_createInfo.subresourceRange = subRange;
}

ImageView::~ImageView()
{
    if (m_handle != VK_NULL_HANDLE)
        destroy();
}

bool ImageView::create(VkImageAspectFlags aspectMask)
{
    if (m_handle != VK_NULL_HANDLE)
        return false;

    m_createInfo.subresourceRange.aspectMask = aspectMask;
    VkResult res = vkCreateImageView(*m_device, &m_createInfo, nullptr, &m_handle);
    return res == VK_SUCCESS;
}

Shader::~Shader()
{
    if (m_module != VK_NULL_HANDLE)
        destroy();
}

bool Shader::create(const std::string& spirvFilepath, VkShaderStageFlagBits stage)
{
    if (m_module != VK_NULL_HANDLE)
        return false;

    // load shader
    std::ifstream file(spirvFilepath, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        LOGE("Could not open file \'" + spirvFilepath + "\'.");
        return false;
    }
    size_t fileSize = file.tellg();
    m_code.resize((fileSize + 3u) / 4u);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(m_code.data()), fileSize);
    file.close();

    // create shader module
    VkShaderModuleCreateInfo moduleInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    moduleInfo.codeSize = m_code.size() * 4u;
    moduleInfo.pCode = m_code.data();
    VkResult res = vkCreateShaderModule(*m_device, &moduleInfo, nullptr, &m_module);
    if (res != VK_SUCCESS)
        return false;

    // update shader stage info
    m_shaderStageInfo.pName = "main";
    m_shaderStageInfo.stage = stage;
    m_shaderStageInfo.module = m_module;

    return true;
}

PipelineLayout::~PipelineLayout()
{
    if (m_handle != VK_NULL_HANDLE)
        destroy();
}

bool PipelineLayout::create(const std::unordered_set<Shader*>& shaders)
{
    if (m_handle != VK_NULL_HANDLE)
        return false;

    // create push descriptor set layout
    // implicity set 0
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (const Shader* sh : shaders)
    {
        spirv_cross::CompilerGLSL comp(sh->m_code.data(), sh->m_code.size());
        spirv_cross::ShaderResources resources = comp.get_shader_resources();
        for (auto& u : resources.uniform_buffers)
        {
            uint32_t set = comp.get_decoration(u.id, spv::DecorationDescriptorSet);
            if (set != 0u)
                continue;

            VkDescriptorSetLayoutBinding uniformBinding{};
            uniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            // TODO arrays
            uniformBinding.descriptorCount = 1u;
            uniformBinding.stageFlags = sh->m_shaderStageInfo.stage;
            uniformBinding.binding = comp.get_decoration(u.id, spv::DecorationBinding);

            bindings.push_back(uniformBinding);
        }

        for (auto& img : resources.sampled_images)
        {
            uint32_t set = comp.get_decoration(img.id, spv::DecorationDescriptorSet);
            if (set != 0u)
                continue;

            VkDescriptorSetLayoutBinding imgBinding{};
            imgBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            // TODO arrays
            imgBinding.descriptorCount = 1u;
            imgBinding.stageFlags = sh->m_shaderStageInfo.stage;
            imgBinding.binding = comp.get_decoration(img.id, spv::DecorationBinding);

            bindings.push_back(imgBinding);
        }

        for (auto& img : resources.storage_images)
        {
            uint32_t set = comp.get_decoration(img.id, spv::DecorationDescriptorSet);
            if (set != 0u)
                continue;

            VkDescriptorSetLayoutBinding imgBinding{};
            imgBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            // TODO arrays
            imgBinding.descriptorCount = 1u;
            imgBinding.stageFlags = sh->m_shaderStageInfo.stage;
            imgBinding.binding = comp.get_decoration(img.id, spv::DecorationBinding);

            bindings.push_back(imgBinding);
        }

        for (auto& as : resources.acceleration_structures)
        {
            uint32_t set = comp.get_decoration(as.id, spv::DecorationDescriptorSet);
            if (set != 0u)
                continue;

            VkDescriptorSetLayoutBinding asBinding{};
            asBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            // TODO arrays
            asBinding.descriptorCount = 1u;
            asBinding.stageFlags = sh->m_shaderStageInfo.stage;
            asBinding.binding = comp.get_decoration(as.id, spv::DecorationBinding);

            bindings.push_back(asBinding);
        }
    }

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    descriptorSetLayoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    descriptorSetLayoutInfo.bindingCount = bindings.size();
    descriptorSetLayoutInfo.pBindings = bindings.data();

    VkResult res = vkCreateDescriptorSetLayout(*m_device, &descriptorSetLayoutInfo, nullptr, &m_descriptorSetLayout);
    if (res != VK_SUCCESS)
        return false;

    // TODO push constants
    VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layoutInfo.setLayoutCount = 1u;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;

    res = vkCreatePipelineLayout(*m_device, &layoutInfo, nullptr, &m_handle);
    return res == VK_SUCCESS;
}

void PipelineLayout::destroy()
{
    vkDestroyDescriptorSetLayout(*m_device, m_descriptorSetLayout, nullptr);
    vkDestroyPipelineLayout(*m_device, m_handle, nullptr);
}

GraphicsPipeline::GraphicsPipeline(Device* device) : m_device(device)
{
    m_inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    m_viewport.x = 0.0f;
    m_viewport.y = 0.0f;
    m_viewport.minDepth = 0.0f;
    m_viewport.maxDepth = 1.0f;

    m_scissor.offset = { 0, 0 };

    m_rasterizerInfo.polygonMode = VK_POLYGON_MODE_FILL;
    m_rasterizerInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    m_rasterizerInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    m_rasterizerInfo.lineWidth = 1.0f;

    m_multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    m_depthInfo.depthTestEnable = VK_TRUE;
    m_depthInfo.depthWriteEnable = VK_TRUE;
    m_depthInfo.depthCompareOp = VK_COMPARE_OP_LESS;

    m_createInfo.pInputAssemblyState = &m_inputAssemblyInfo;
    m_createInfo.pRasterizationState = &m_rasterizerInfo;
    m_createInfo.pMultisampleState = &m_multisampleInfo;
    m_createInfo.pDepthStencilState = &m_depthInfo;
}

GraphicsPipeline::~GraphicsPipeline()
{
    if (m_handle != VK_NULL_HANDLE)
        destroy();
}

bool GraphicsPipeline::create(const std::unordered_set<Shader*>& shaders)
{
    if (m_handle != VK_NULL_HANDLE)
        return false;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    if (!m_vertexBindings.empty())
    {
        vertexInputInfo.vertexBindingDescriptionCount = m_vertexBindings.size();
        vertexInputInfo.pVertexBindingDescriptions = m_vertexBindings.data();
    }
    if (!m_vertexAttributes.empty())
    {
        vertexInputInfo.vertexAttributeDescriptionCount = m_vertexAttributes.size();
        vertexInputInfo.pVertexAttributeDescriptions = m_vertexAttributes.data();
    }

    VkPipelineViewportStateCreateInfo viewportInfo{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportInfo.viewportCount = 1u;
    viewportInfo.pViewports = &m_viewport;
    viewportInfo.scissorCount = 1u;
    viewportInfo.pScissors = &m_scissor;

    VkPipelineColorBlendStateCreateInfo colorBlendInfo{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    if (!m_colorBlendAttachmentStates.empty())
    {
        colorBlendInfo.attachmentCount = m_colorBlendAttachmentStates.size();
        colorBlendInfo.pAttachments = m_colorBlendAttachmentStates.data();
    }

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    for (const Shader* sh : shaders)
        shaderStages.push_back(sh->m_shaderStageInfo);

    m_createInfo.stageCount = shaderStages.size();
    m_createInfo.pStages = shaderStages.data();
    m_createInfo.pVertexInputState = &vertexInputInfo;
    m_createInfo.pViewportState = &viewportInfo;
    m_createInfo.pColorBlendState = &colorBlendInfo;
    m_createInfo.layout = *m_layout;

    VkResult res = vkCreateGraphicsPipelines(*m_device, VK_NULL_HANDLE, 1u, &m_createInfo, nullptr, &m_handle);
    return res == VK_SUCCESS;
}

bool GraphicsPipeline::create(const std::unordered_set<Shader*>& shaders, std::shared_ptr<PipelineLayout> layout, uint32_t width, uint32_t height)
{
    if (m_handle != VK_NULL_HANDLE)
        return false;

    m_layout = layout;
    m_viewport.width = width;
    m_viewport.height = height;
    m_scissor.extent = { width, height };

    return create(shaders);
}

bool GraphicsPipeline::create(const std::unordered_set<Shader*>& shaders, uint32_t width, uint32_t height)
{
    if (m_handle != VK_NULL_HANDLE)
        return false;

    m_layout = std::make_shared<PipelineLayout>(m_device);
    if (!m_layout->create(shaders))
        return false;

    m_viewport.width = width;
    m_viewport.height = height;
    m_scissor.extent = { width, height };

    return create(shaders);
}

bool CommandBuffer::create()
{
    if (m_handle != VK_NULL_HANDLE)
        return false;

    VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocInfo.commandBufferCount = 1u;
    allocInfo.commandPool = m_cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    VkResult res = vkAllocateCommandBuffers(*m_device, &allocInfo, &m_handle);
    return res == VK_SUCCESS;
}

void CommandBuffer::bindGraphicsPipeline(GraphicsPipeline* pipeline)
{
    vkCmdBindPipeline(m_handle, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline);
    m_boundPipeline = *pipeline;
    m_boundLayout = *pipeline->m_layout;
}

void CommandBuffer::imageMemoryBarrier(VkImage img, VkImageAspectFlags aspectMask, VkPipelineStageFlags2 srcStageMask, VkAccessFlags2 srcAccessMask, VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 dstAccessMask, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t arrayLayers, uint32_t mipLevels)
{
    VkImageSubresourceRange subRange{};
    subRange.aspectMask = aspectMask;
    subRange.baseArrayLayer = 0u;
    subRange.baseMipLevel = 0u;
    subRange.layerCount = arrayLayers;
    subRange.levelCount = mipLevels;

    VkImageMemoryBarrier2 imageMemoryBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    imageMemoryBarrier.srcStageMask = srcStageMask;
    imageMemoryBarrier.srcAccessMask = srcAccessMask;
    imageMemoryBarrier.dstStageMask = dstStageMask;
    imageMemoryBarrier.dstAccessMask = dstAccessMask;
    imageMemoryBarrier.oldLayout = oldLayout;
    imageMemoryBarrier.newLayout = newLayout;
    imageMemoryBarrier.image = img;
    imageMemoryBarrier.subresourceRange = subRange;

    VkDependencyInfo dependencyInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dependencyInfo.imageMemoryBarrierCount = 1u;
    dependencyInfo.pImageMemoryBarriers = &imageMemoryBarrier;

    vkCmdPipelineBarrier2(m_handle, &dependencyInfo);
}

void CommandBuffer::imageMemoryBarrier(Image& img, VkImageAspectFlags aspectMask, VkPipelineStageFlags2 srcStageMask, VkAccessFlags2 srcAccessMask, VkPipelineStageFlags2 dstStageMask, VkAccessFlags2 dstAccessMask, VkImageLayout newLayout)
{
    imageMemoryBarrier(img, aspectMask, srcStageMask, srcAccessMask, dstStageMask, dstAccessMask, img.m_layout, newLayout, img.m_createInfo.arrayLayers, img.m_createInfo.mipLevels);
    img.m_layout = newLayout;
}

//RenderContext::RenderContext(GLFWwindow* window)
//{
//    createInstance();
//    createSurface(window);
//    getPhysicalDevice();
//    chooseGctPresentQueue();
//    createDeviceAndQueue();
//    createCommandBuffer();
//    createSwapchain();
//
//    VmaAllocatorCreateInfo allocatorInfo{};
//    allocatorInfo.device = m_device;
//    allocatorInfo.physicalDevice = m_physicalDevice;
//    allocatorInfo.instance = m_instance;
//    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
//
//    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &m_allocator), "failed to create VMA allocator!");
//}
//
//RenderContext::~RenderContext()
//{
//    vmaDestroyAllocator(m_allocator);
//
//    vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
//    vkDestroyCommandPool(m_device, m_cmdPool, nullptr);
//    vkDestroyCommandPool(m_device, m_cmdPoolTransient, nullptr);
//    vkDestroyDevice(m_device, nullptr);
//    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
//    vkDestroyInstance(m_instance, nullptr);
//}
//
//void RenderContext::createInstance()
//{
//    VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
//    appInfo.apiVersion = VK_MAKE_API_VERSION(1, 2, 0, 0);
//    appInfo.applicationVersion = 0u;
//    appInfo.pApplicationName = "cray";
//
//    uint32_t glfwExtensionCount;
//    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
//    if (glfwExtensionCount == 0)
//        throw std::runtime_error("failed to get required GLFW extensions!");
//
//    VkInstanceCreateInfo instanceInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
//    instanceInfo.enabledLayerCount = ENABLED_LAYER_COUNT;
//    instanceInfo.ppEnabledLayerNames = ENABLED_LAYER_NAMES;
//    instanceInfo.enabledExtensionCount = glfwExtensionCount;
//    instanceInfo.ppEnabledExtensionNames = glfwExtensions;
//    instanceInfo.pApplicationInfo = &appInfo;
//
//    VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &m_instance), "failed to create Vulkan instance!");
//}
//
//void RenderContext::createSurface(GLFWwindow* window)
//{
//    VK_CHECK(glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface), "failed to create window surface!");
//}
//
//void RenderContext::getPhysicalDevice()
//{
//    uint32_t physicalDeviceCount;
//    VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &physicalDeviceCount, nullptr), "failed to enumerate physical devices!");
//    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
//    vkEnumeratePhysicalDevices(m_instance, &physicalDeviceCount, physicalDevices.data());
//
//    int physicalDeviceIdx = -1;
//    for (uint32_t i = 0; i < physicalDeviceCount; i++)
//    {
//        VkPhysicalDeviceProperties props;
//        vkGetPhysicalDeviceProperties(physicalDevices[i], &props);
//        // TODO make sure we select the right discrete GPU
//        // with required properties, features etc.!
//        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
//        {
//            physicalDeviceIdx = i;
//            break;
//        }
//    }
//
//    if (physicalDeviceIdx == -1)
//        throw std::runtime_error("failed to find appropriate GPU!");
//
//    m_physicalDevice = physicalDevices[physicalDeviceIdx];
//
//    VkPhysicalDeviceAccelerationStructurePropertiesKHR ASProps{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
//    VkPhysicalDeviceProperties2 props{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
//    props.pNext = &ASProps;
//    vkGetPhysicalDeviceProperties2(m_physicalDevice, &props);
//    m_ASProperties = std::move(ASProps);
//}
//
//void RenderContext::chooseGctPresentQueue()
//{
//    uint32_t queueFamilyCount;
//    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
//    std::vector<VkQueueFamilyProperties> queueFamilyProps(queueFamilyCount);
//    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilyProps.data());
//
//    for (uint32_t i = 0; i < queueFamilyCount; i++)
//    {
//        // TODO make sure we select the right GCT/present queue
//        if ((queueFamilyProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (queueFamilyProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
//        {
//            VkBool32 surfaceSupported;
//            VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &surfaceSupported), "failed to get physical device surface support!");
//            if (surfaceSupported != VK_TRUE)
//                throw std::runtime_error("surface not supported for physical device!");
//            m_queueFamilyIdx = i;
//            break;
//        }
//    }
//}
//
//void RenderContext::createDeviceAndQueue()
//{
//    const float queuePriority = 0.0f;
//    VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
//    queueInfo.queueFamilyIndex = m_queueFamilyIdx;
//    queueInfo.queueCount = 1u;
//    queueInfo.pQueuePriorities = &queuePriority;
//
//    VkPhysicalDeviceAccelerationStructureFeaturesKHR ASFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
//    ASFeatures.accelerationStructure = VK_TRUE;
//
//    VkPhysicalDeviceBufferDeviceAddressFeatures bufferAddrFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES };
//    bufferAddrFeatures.bufferDeviceAddress = VK_TRUE;
//    bufferAddrFeatures.pNext = &ASFeatures;
//
//    VkPhysicalDeviceRayQueryFeaturesKHR RQFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
//    RQFeatures.rayQuery = VK_TRUE;
//    RQFeatures.pNext = &bufferAddrFeatures;
//
//    VkPhysicalDeviceFeatures2 deviceFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
//    deviceFeatures.pNext = &RQFeatures;
//
//    VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
//    deviceInfo.queueCreateInfoCount = 1u;
//    deviceInfo.pQueueCreateInfos = &queueInfo;
//    deviceInfo.enabledExtensionCount = ENABLED_DEVICE_EXTENSION_COUNT;
//    deviceInfo.ppEnabledExtensionNames = ENABLED_DEVICE_EXTENSION_NAMES;
//    deviceInfo.pNext = &deviceFeatures;
//
//    VK_CHECK(vkCreateDevice(m_physicalDevice, &deviceInfo, nullptr, &m_device), "failed to create device!");
//    vkGetDeviceQueue(m_device, m_queueFamilyIdx, 0u, &m_queue);
//}
//
//void RenderContext::createCommandBuffer()
//{
//    VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
//    poolInfo.queueFamilyIndex = m_queueFamilyIdx;
//    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
//
//    VK_CHECK(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_cmdPool), "failed to create command pool!");
//
//    VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
//    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
//    allocInfo.commandPool = m_cmdPool;
//    allocInfo.commandBufferCount = 1u;
//
//    VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &m_cmdBuf), "failed to allocate command buffer!");
//
//    // also create command pool to allocate short-lived command buffers for transferring staging buffers/images to GPU
//    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
//    VK_CHECK(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_cmdPoolTransient), "failed to create transient command pool!");
//}
//
//void RenderContext::createSwapchain()
//{
//    // TODO check surface, swapchain, and physical device support
//    VkSurfaceCapabilitiesKHR surfaceCapabilities;
//    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &surfaceCapabilities), "failed to get physical device surface capabilities!");
//    m_extent = surfaceCapabilities.currentExtent;
//
//    VkSwapchainCreateInfoKHR swapchainInfo{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
//    swapchainInfo.surface = m_surface;
//    swapchainInfo.minImageCount = surfaceCapabilities.minImageCount + 1u;
//    swapchainInfo.imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
//    swapchainInfo.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
//    swapchainInfo.imageExtent = m_extent;
//    swapchainInfo.imageArrayLayers = 1u;
//    swapchainInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
//    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
//    swapchainInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
//    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
//    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
//    swapchainInfo.clipped = VK_TRUE;
//
//    VK_CHECK(vkCreateSwapchainKHR(m_device, &swapchainInfo, nullptr, &m_swapchain), "failed to create swapchain!");
//
//    uint32_t swapchainImageCount;
//    VK_CHECK(vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount, nullptr), "failed to get swapchain images!");
//    m_swapchainImages.resize(swapchainImageCount);
//    vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount, m_swapchainImages.data());
//}
//
//uint32_t RenderContext::acquireNextSwapchainImage(VkSemaphore acquiredSemaphore) const
//{
//    uint32_t swapIdx;
//    VK_CHECK(vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, acquiredSemaphore, VK_NULL_HANDLE, &swapIdx), "failed to acquire swapchain image!");
//    return swapIdx;
//}
//
//void RenderContext::submitToQueue(VkSemaphore waitSemaphore, VkPipelineStageFlags waitStageMask, VkSemaphore signalSemaphore, VkFence fence) const
//{
//    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
//    submitInfo.waitSemaphoreCount = 1u;
//    submitInfo.pWaitSemaphores = &waitSemaphore;
//    submitInfo.pWaitDstStageMask = &waitStageMask;
//    submitInfo.commandBufferCount = 1u;
//    submitInfo.pCommandBuffers = &m_cmdBuf;
//    submitInfo.signalSemaphoreCount = 1u;
//    submitInfo.pSignalSemaphores = &signalSemaphore;
//
//    VK_CHECK(vkQueueSubmit(m_queue, 1u, &submitInfo, fence), "failed to submit to queue!");
//}
//
//void RenderContext::present(uint32_t swapIdx, VkSemaphore waitSemaphore) const
//{
//    VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
//    presentInfo.waitSemaphoreCount = 1u;
//    presentInfo.pWaitSemaphores = &waitSemaphore;
//    presentInfo.swapchainCount = 1u;
//    presentInfo.pSwapchains = &m_swapchain;
//    presentInfo.pImageIndices = &swapIdx;
//
//    VK_CHECK(vkQueuePresentKHR(m_queue, &presentInfo), "failed to present swapchain!");
//}
//
//std::shared_ptr<Buffer> RenderContext::createBuffer(VkDeviceSize size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkMemoryPropertyFlags memoryFlags, VkDeviceSize minAlignment) const
//{
//    std::shared_ptr<Buffer> buf = std::make_shared<Buffer>(m_allocator, size, bufferUsage, memoryUsage, allocFlags, memoryFlags, minAlignment);
//    return buf;
//}
//
//std::shared_ptr<Image> RenderContext::createImage(VkFormat format, VkExtent3D extent, VkImageUsageFlags imageUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkMemoryPropertyFlags memoryFlags, VkImageLayout initialLayout, VkImageTiling tiling, uint32_t mipLevels, uint32_t arrayLayers, VkSampleCountFlagBits samples, VkImageType imageType) const
//{
//    std::shared_ptr<Image> img = std::make_shared<Image>(m_allocator, format, extent, imageUsage, memoryUsage, allocFlags, memoryFlags, initialLayout, tiling, mipLevels, arrayLayers, samples, imageType);
//    return img;
//}
//
//std::shared_ptr<ImageView> RenderContext::createImageView(vk::Image& image, VkImageAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t levelCount, uint32_t baseArrayLayer, uint32_t layerCount, VkImageViewType viewType) const
//{
//    std::shared_ptr<ImageView> view = std::make_shared<ImageView>(m_device, image, aspectMask, baseMipLevel, levelCount, baseArrayLayer, layerCount, viewType);
//    return view;
//}
//
//void RenderContext::copyStagingBuffer(const Buffer& dst, const Buffer& src, VkDeviceSize size, VkDeviceSize dstOffset, VkDeviceSize srcOffset) const
//{
//    VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
//    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
//    allocInfo.commandPool = m_cmdPoolTransient;
//    allocInfo.commandBufferCount = 1u;
//
//    VkCommandBuffer cmd;
//    VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &cmd), "failed to allocate copy command buffer!");
//
//    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
//    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
//    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "failed to begin copy command buffer!");
//
//    VkBufferCopy copy{};
//    copy.srcOffset = srcOffset;
//    copy.dstOffset = dstOffset;
//    copy.size = size;
//
//    vkCmdCopyBuffer(cmd, src, dst, 1u, &copy);
//    VK_CHECK(vkEndCommandBuffer(cmd), "failed to end copy command buffer!");
//
//    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
//    submitInfo.commandBufferCount = 1u;
//    submitInfo.pCommandBuffers = &cmd;
//
//    VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
//    VkFence fence;
//    VK_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &fence), "failed to create copy fence!");
//
//    VK_CHECK(vkQueueSubmit(m_queue, 1u, &submitInfo, fence), "failed to submit copy to queue!");
//    VK_CHECK(vkWaitForFences(m_device, 1u, &fence, VK_TRUE, UINT64_MAX), "failed to wait for copy fence!");
//
//    vkDestroyFence(m_device, fence, nullptr);
//    vkFreeCommandBuffers(m_device, m_cmdPoolTransient, 1u, &cmd);
//}
//
//void RenderContext::copyStagingImage(Image& dst, Image& src, VkExtent3D extent, VkImageAspectFlags dstAspectMask, VkImageAspectFlags srcAspectMask, VkImageLayout dstFinalLayout, uint32_t dstMipLevel, uint32_t srcMipLevel, VkOffset3D dstOffset, VkOffset3D srcOffset, uint32_t dstBaseArrayLayer, uint32_t srcBaseArrayLayer, uint32_t dstLayerCount, uint32_t srcLayerCount) const
//{
//    VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
//    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
//    allocInfo.commandPool = m_cmdPoolTransient;
//    allocInfo.commandBufferCount = 1u;
//
//    VkCommandBuffer cmdHandle;
//    VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &cmdHandle), "failed to allocate copy command buffer!");
//    CommandBuffer cmd(cmdHandle);
//
//    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
//    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
//    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "failed to begin copy command buffer!");
//
//    cmd.imageMemoryBarrier(src, srcAspectMask, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0u, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcMipLevel, 1u, srcBaseArrayLayer, srcLayerCount);
//
//    cmd.imageMemoryBarrier(dst, dstAspectMask, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0u, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dstMipLevel, 1u, dstBaseArrayLayer, dstLayerCount);
//
//    VkImageSubresourceLayers dstSubLayers{};
//    dstSubLayers.aspectMask = dstAspectMask;
//    dstSubLayers.baseArrayLayer = dstBaseArrayLayer;
//    dstSubLayers.layerCount = dstLayerCount;
//    dstSubLayers.mipLevel = dstMipLevel;
//    VkImageSubresourceLayers srcSubLayers{};
//    srcSubLayers.aspectMask = srcAspectMask;
//    srcSubLayers.baseArrayLayer = srcBaseArrayLayer;
//    srcSubLayers.layerCount = srcLayerCount;
//    srcSubLayers.mipLevel = srcMipLevel;
//    VkImageCopy copy{};
//    copy.srcOffset = srcOffset;
//    copy.dstOffset = dstOffset;
//    copy.extent = extent;
//    copy.srcSubresource = srcSubLayers;
//    copy.dstSubresource = dstSubLayers;
//
//    vkCmdCopyImage(cmd, src, src.m_layout, dst, dst.m_layout, 1u, &copy);
//
//    cmd.imageMemoryBarrier(dst, dstAspectMask, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0u, dstFinalLayout, dstMipLevel, 1u, dstBaseArrayLayer, dstLayerCount);
//
//    VK_CHECK(vkEndCommandBuffer(cmd), "failed to end copy command buffer!");
//
//    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
//    submitInfo.commandBufferCount = 1u;
//    submitInfo.pCommandBuffers = &cmd.m_handle;
//
//    VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
//    VkFence fence;
//    VK_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &fence), "failed to create copy fence!");
//
//    VK_CHECK(vkQueueSubmit(m_queue, 1u, &submitInfo, fence), "failed to submit copy to queue!");
//    VK_CHECK(vkWaitForFences(m_device, 1u, &fence, VK_TRUE, UINT64_MAX), "failed to wait for copy fence!");
//
//    vkDestroyFence(m_device, fence, nullptr);
//    vkFreeCommandBuffers(m_device, m_cmdPoolTransient, 1u, &cmd.m_handle);
//}
//
//void RenderContext::buildAS(VkAccelerationStructureBuildGeometryInfoKHR buildInfo, VkAccelerationStructureBuildRangeInfoKHR* rangeInfo) const
//{
//    static PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(m_device, "vkCmdBuildAccelerationStructuresKHR"));
//
//    VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
//    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
//    allocInfo.commandPool = m_cmdPoolTransient;
//    allocInfo.commandBufferCount = 1u;
//
//    VkCommandBuffer cmd;
//    VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &cmd), "failed to allocate build AS command buffer!");
//
//    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
//    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
//    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "failed to begin build AS command buffer!");
//
//    vkCmdBuildAccelerationStructuresKHR(cmd, 1u, &buildInfo, &rangeInfo);
//
//    VK_CHECK(vkEndCommandBuffer(cmd), "failed to end build AS command buffer!");
//
//    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
//    submitInfo.commandBufferCount = 1u;
//    submitInfo.pCommandBuffers = &cmd;
//
//    VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
//    VkFence fence;
//    VK_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &fence), "failed to create build AS fence!");
//
//    VK_CHECK(vkQueueSubmit(m_queue, 1u, &submitInfo, fence), "failed to submit build AS to queue!");
//    VK_CHECK(vkWaitForFences(m_device, 1u, &fence, VK_TRUE, UINT64_MAX), "failed to wait for build AS fence!");
//
//    vkDestroyFence(m_device, fence, nullptr);
//    vkFreeCommandBuffers(m_device, m_cmdPoolTransient, 1u, &cmd);
//}

}
