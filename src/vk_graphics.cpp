#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include "vk_graphics.h"

#include "scene.h"

#include <spirv_cross/spirv_glsl.hpp>

#ifndef NDEBUG
#define ENABLED_LAYER_COUNT 1u
static const char* ENABLED_LAYER_NAMES[] = { "VK_LAYER_KHRONOS_validation" };
#else
#define ENABLED_LAYER_COUNT 0u
static const chair** ENABLED_LAYER_NAMES = nullptr;
#endif

#define ENABLED_DEVICE_EXTENSION_COUNT 4u
static const char* ENABLED_DEVICE_EXTENSION_NAMES[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME };

namespace vk
{

RenderContext::RenderContext(GLFWwindow* window)
{
    createInstance();
    createSurface(window);
    getPhysicalDevice();
    chooseGctPresentQueue();
    createDeviceAndQueue();
    createCommandBuffer();
    createSwapchain();

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.device = m_device;
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.instance = m_instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &m_allocator), "failed to create VMA allocator!");
}

RenderContext::~RenderContext()
{
    vmaDestroyAllocator(m_allocator);

    vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    vkDestroyCommandPool(m_device, m_cmdPool, nullptr);
    vkDestroyCommandPool(m_device, m_cmdPoolTransient, nullptr);
    vkDestroyDevice(m_device, nullptr);
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyInstance(m_instance, nullptr);
}

void RenderContext::createInstance()
{
    VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.apiVersion = VK_MAKE_API_VERSION(1, 2, 0, 0);
    appInfo.applicationVersion = 0u;
    appInfo.pApplicationName = "cray";

    uint32_t glfwExtensionCount;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (glfwExtensionCount == 0)
        throw std::runtime_error("failed to get required GLFW extensions!");

    VkInstanceCreateInfo instanceInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    instanceInfo.enabledLayerCount = ENABLED_LAYER_COUNT;
    instanceInfo.ppEnabledLayerNames = ENABLED_LAYER_NAMES;
    instanceInfo.enabledExtensionCount = glfwExtensionCount;
    instanceInfo.ppEnabledExtensionNames = glfwExtensions;
    instanceInfo.pApplicationInfo = &appInfo;

    VK_CHECK(vkCreateInstance(&instanceInfo, nullptr, &m_instance), "failed to create Vulkan instance!");
}

void RenderContext::createSurface(GLFWwindow* window)
{
    VK_CHECK(glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface), "failed to create window surface!");
}

void RenderContext::getPhysicalDevice()
{
    uint32_t physicalDeviceCount;
    VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &physicalDeviceCount, nullptr), "failed to enumerate physical devices!");
    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    vkEnumeratePhysicalDevices(m_instance, &physicalDeviceCount, physicalDevices.data());

    int physicalDeviceIdx = -1;
    for (uint32_t i = 0; i < physicalDeviceCount; i++)
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physicalDevices[i], &props);
        // TODO make sure we select the right discrete GPU
        // with required properties, features etc.!
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            physicalDeviceIdx = i;
            break;
        }
    }

    if (physicalDeviceIdx == -1)
        throw std::runtime_error("failed to find appropriate GPU!");

    m_physicalDevice = physicalDevices[physicalDeviceIdx];

    VkPhysicalDeviceAccelerationStructurePropertiesKHR ASProps{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
    VkPhysicalDeviceProperties2 props{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
    props.pNext = &ASProps;
    vkGetPhysicalDeviceProperties2(m_physicalDevice, &props);
    m_ASProperties = std::move(ASProps);
}

void RenderContext::chooseGctPresentQueue()
{
    uint32_t queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilyProps(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilyProps.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++)
    {
        // TODO make sure we select the right GCT/present queue
        if ((queueFamilyProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && (queueFamilyProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
        {
            VkBool32 surfaceSupported;
            VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &surfaceSupported), "failed to get physical device surface support!");
            if (surfaceSupported != VK_TRUE)
                throw std::runtime_error("surface not supported for physical device!");
            m_queueFamilyIdx = i;
            break;
        }
    }
}

void RenderContext::createDeviceAndQueue()
{
    const float queuePriority = 0.0f;
    VkDeviceQueueCreateInfo queueInfo{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    queueInfo.queueFamilyIndex = m_queueFamilyIdx;
    queueInfo.queueCount = 1u;
    queueInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR ASFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
    ASFeatures.accelerationStructure = VK_TRUE;

    VkPhysicalDeviceBufferDeviceAddressFeatures bufferAddrFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES };
    bufferAddrFeatures.bufferDeviceAddress = VK_TRUE;
    bufferAddrFeatures.pNext = &ASFeatures;

    VkPhysicalDeviceRayQueryFeaturesKHR RQFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
    RQFeatures.rayQuery = VK_TRUE;
    RQFeatures.pNext = &bufferAddrFeatures;

    VkPhysicalDeviceFeatures2 deviceFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    deviceFeatures.pNext = &RQFeatures;

    VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    deviceInfo.queueCreateInfoCount = 1u;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = ENABLED_DEVICE_EXTENSION_COUNT;
    deviceInfo.ppEnabledExtensionNames = ENABLED_DEVICE_EXTENSION_NAMES;
    deviceInfo.pNext = &deviceFeatures;

    VK_CHECK(vkCreateDevice(m_physicalDevice, &deviceInfo, nullptr, &m_device), "failed to create device!");
    vkGetDeviceQueue(m_device, m_queueFamilyIdx, 0u, &m_queue);
}

void RenderContext::createCommandBuffer()
{
    VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.queueFamilyIndex = m_queueFamilyIdx;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VK_CHECK(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_cmdPool), "failed to create command pool!");

    VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_cmdPool;
    allocInfo.commandBufferCount = 1u;

    VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &m_cmdBuf), "failed to allocate command buffer!");

    // also create command pool to allocate short-lived command buffers for transferring staging buffers/images to GPU
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VK_CHECK(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_cmdPoolTransient), "failed to create transient command pool!");
}

void RenderContext::createSwapchain()
{
    // TODO check surface, swapchain, and physical device support
    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &surfaceCapabilities), "failed to get physical device surface capabilities!");
    m_extent = surfaceCapabilities.currentExtent;

    VkSwapchainCreateInfoKHR swapchainInfo{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    swapchainInfo.surface = m_surface;
    swapchainInfo.minImageCount = surfaceCapabilities.minImageCount + 1u;
    swapchainInfo.imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
    swapchainInfo.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
    swapchainInfo.imageExtent = m_extent;
    swapchainInfo.imageArrayLayers = 1u;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainInfo.clipped = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(m_device, &swapchainInfo, nullptr, &m_swapchain), "failed to create swapchain!");

    uint32_t swapchainImageCount;
    VK_CHECK(vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount, nullptr), "failed to get swapchain images!");
    m_swapchainImages.resize(swapchainImageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount, m_swapchainImages.data());
}

uint32_t RenderContext::acquireNextSwapchainImage(VkSemaphore acquiredSemaphore) const
{
    uint32_t swapIdx;
    VK_CHECK(vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, acquiredSemaphore, VK_NULL_HANDLE, &swapIdx), "failed to acquire swapchain image!");
    return swapIdx;
}

void RenderContext::submitToQueue(VkSemaphore waitSemaphore, VkPipelineStageFlags waitStageMask, VkSemaphore signalSemaphore, VkFence fence) const
{
    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.waitSemaphoreCount = 1u;
    submitInfo.pWaitSemaphores = &waitSemaphore;
    submitInfo.pWaitDstStageMask = &waitStageMask;
    submitInfo.commandBufferCount = 1u;
    submitInfo.pCommandBuffers = &m_cmdBuf;
    submitInfo.signalSemaphoreCount = 1u;
    submitInfo.pSignalSemaphores = &signalSemaphore;

    VK_CHECK(vkQueueSubmit(m_queue, 1u, &submitInfo, fence), "failed to submit to queue!");
}

void RenderContext::present(uint32_t swapIdx, VkSemaphore waitSemaphore) const
{
    VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    presentInfo.waitSemaphoreCount = 1u;
    presentInfo.pWaitSemaphores = &waitSemaphore;
    presentInfo.swapchainCount = 1u;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &swapIdx;

    VK_CHECK(vkQueuePresentKHR(m_queue, &presentInfo), "failed to present swapchain!");
}

std::shared_ptr<Buffer> RenderContext::createBuffer(VkDeviceSize size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkMemoryPropertyFlags memoryFlags, VkDeviceSize minAlignment) const
{
    std::shared_ptr<Buffer> buf = std::make_shared<Buffer>(m_allocator, size, bufferUsage, memoryUsage, allocFlags, memoryFlags, minAlignment);
    return buf;
}

std::shared_ptr<Image> RenderContext::createImage(VkFormat format, VkExtent3D extent, VkImageUsageFlags imageUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkMemoryPropertyFlags memoryFlags, VkImageLayout initialLayout, VkImageTiling tiling, uint32_t mipLevels, uint32_t arrayLayers, VkSampleCountFlagBits samples, VkImageType imageType) const
{
    std::shared_ptr<Image> img = std::make_shared<Image>(m_allocator, format, extent, imageUsage, memoryUsage, allocFlags, memoryFlags, initialLayout, tiling, mipLevels, arrayLayers, samples, imageType);
    return img;
}

std::shared_ptr<ImageView> RenderContext::createImageView(vk::Image& image, VkImageAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t levelCount, uint32_t baseArrayLayer, uint32_t layerCount, VkImageViewType viewType) const
{
    std::shared_ptr<ImageView> view = std::make_shared<ImageView>(m_device, image, aspectMask, baseMipLevel, levelCount, baseArrayLayer, layerCount, viewType);
    return view;
}

void RenderContext::copyStagingBuffer(const Buffer& dst, const Buffer& src, VkDeviceSize size, VkDeviceSize dstOffset, VkDeviceSize srcOffset) const
{
    VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_cmdPoolTransient;
    allocInfo.commandBufferCount = 1u;

    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &cmd), "failed to allocate copy command buffer!");

    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "failed to begin copy command buffer!");

    VkBufferCopy copy{};
    copy.srcOffset = srcOffset;
    copy.dstOffset = dstOffset;
    copy.size = size;

    vkCmdCopyBuffer(cmd, src, dst, 1u, &copy);
    VK_CHECK(vkEndCommandBuffer(cmd), "failed to end copy command buffer!");

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1u;
    submitInfo.pCommandBuffers = &cmd;

    VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    VK_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &fence), "failed to create copy fence!");

    VK_CHECK(vkQueueSubmit(m_queue, 1u, &submitInfo, fence), "failed to submit copy to queue!");
    VK_CHECK(vkWaitForFences(m_device, 1u, &fence, VK_TRUE, UINT64_MAX), "failed to wait for copy fence!");

    vkDestroyFence(m_device, fence, nullptr);
    vkFreeCommandBuffers(m_device, m_cmdPoolTransient, 1u, &cmd);
}

void RenderContext::copyStagingImage(Image& dst, Image& src, VkExtent3D extent, VkImageAspectFlags dstAspectMask, VkImageAspectFlags srcAspectMask, VkImageLayout dstFinalLayout, uint32_t dstMipLevel, uint32_t srcMipLevel, VkOffset3D dstOffset, VkOffset3D srcOffset, uint32_t dstBaseArrayLayer, uint32_t srcBaseArrayLayer, uint32_t dstLayerCount, uint32_t srcLayerCount) const
{
    VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_cmdPoolTransient;
    allocInfo.commandBufferCount = 1u;

    VkCommandBuffer cmdHandle;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &cmdHandle), "failed to allocate copy command buffer!");
    CommandBuffer cmd(cmdHandle);

    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "failed to begin copy command buffer!");

    cmd.imageMemoryBarrier(src, srcAspectMask, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0u, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcMipLevel, 1u, srcBaseArrayLayer, srcLayerCount);

    cmd.imageMemoryBarrier(dst, dstAspectMask, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0u, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dstMipLevel, 1u, dstBaseArrayLayer, dstLayerCount);

    VkImageSubresourceLayers dstSubLayers{};
    dstSubLayers.aspectMask = dstAspectMask;
    dstSubLayers.baseArrayLayer = dstBaseArrayLayer;
    dstSubLayers.layerCount = dstLayerCount;
    dstSubLayers.mipLevel = dstMipLevel;
    VkImageSubresourceLayers srcSubLayers{};
    srcSubLayers.aspectMask = srcAspectMask;
    srcSubLayers.baseArrayLayer = srcBaseArrayLayer;
    srcSubLayers.layerCount = srcLayerCount;
    srcSubLayers.mipLevel = srcMipLevel;
    VkImageCopy copy{};
    copy.srcOffset = srcOffset;
    copy.dstOffset = dstOffset;
    copy.extent = extent;
    copy.srcSubresource = srcSubLayers;
    copy.dstSubresource = dstSubLayers;

    vkCmdCopyImage(cmd, src, src.m_layout, dst, dst.m_layout, 1u, &copy);

    cmd.imageMemoryBarrier(dst, dstAspectMask, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0u, dstFinalLayout, dstMipLevel, 1u, dstBaseArrayLayer, dstLayerCount);

    VK_CHECK(vkEndCommandBuffer(cmd), "failed to end copy command buffer!");

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1u;
    submitInfo.pCommandBuffers = &cmd.m_handle;

    VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    VK_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &fence), "failed to create copy fence!");

    VK_CHECK(vkQueueSubmit(m_queue, 1u, &submitInfo, fence), "failed to submit copy to queue!");
    VK_CHECK(vkWaitForFences(m_device, 1u, &fence, VK_TRUE, UINT64_MAX), "failed to wait for copy fence!");

    vkDestroyFence(m_device, fence, nullptr);
    vkFreeCommandBuffers(m_device, m_cmdPoolTransient, 1u, &cmd.m_handle);
}

void RenderContext::buildAS(VkAccelerationStructureBuildGeometryInfoKHR buildInfo, VkAccelerationStructureBuildRangeInfoKHR* rangeInfo) const
{
    static PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(m_device, "vkCmdBuildAccelerationStructuresKHR"));

    VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_cmdPoolTransient;
    allocInfo.commandBufferCount = 1u;

    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &cmd), "failed to allocate build AS command buffer!");

    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo), "failed to begin build AS command buffer!");

    vkCmdBuildAccelerationStructuresKHR(cmd, 1u, &buildInfo, &rangeInfo);

    VK_CHECK(vkEndCommandBuffer(cmd), "failed to end build AS command buffer!");

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1u;
    submitInfo.pCommandBuffers = &cmd;

    VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    VK_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &fence), "failed to create build AS fence!");

    VK_CHECK(vkQueueSubmit(m_queue, 1u, &submitInfo, fence), "failed to submit build AS to queue!");
    VK_CHECK(vkWaitForFences(m_device, 1u, &fence, VK_TRUE, UINT64_MAX), "failed to wait for build AS fence!");

    vkDestroyFence(m_device, fence, nullptr);
    vkFreeCommandBuffers(m_device, m_cmdPoolTransient, 1u, &cmd);
}

Buffer::Buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkMemoryPropertyFlags memoryFlags, VkDeviceSize minAlignment) : m_allocator(allocator), m_size(size)
{
    VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bufferInfo.size = m_size;
    bufferInfo.usage = bufferUsage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;
    allocInfo.flags = allocFlags;
    allocInfo.requiredFlags = memoryFlags;

    if (minAlignment != 0)
    {
        VK_CHECK(vmaCreateBufferWithAlignment(m_allocator, &bufferInfo, &allocInfo, minAlignment, &m_handle, &m_allocation, nullptr), "failed to create aligned VMA buffer!");
    }
    else
    {
        VK_CHECK(vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &m_handle, &m_allocation, nullptr), "failed to create VMA buffer!");
    }
}

Buffer::~Buffer()
{
    vmaDestroyBuffer(m_allocator, m_handle, m_allocation);
}

void* Buffer::map() const
{
    void* data;
    VK_CHECK(vmaMapMemory(m_allocator, m_allocation, &data), "failed to map VMA buffer!");
    return data;
}

void Buffer::unmap() const
{
    vmaUnmapMemory(m_allocator, m_allocation);
}

Image::Image(VmaAllocator allocator, VkFormat format, VkExtent3D extent, VkImageUsageFlags imageUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkMemoryPropertyFlags memoryFlags, VkImageLayout initialLayout, VkImageTiling tiling, uint32_t mipLevels, uint32_t arrayLayers, VkSampleCountFlagBits samples, VkImageType imageType) : m_allocator(allocator), m_layout(initialLayout)
{
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;
    allocInfo.flags = allocFlags;
    allocInfo.requiredFlags = memoryFlags;

    m_imageInfo.imageType = imageType;
    m_imageInfo.format = format;
    m_imageInfo.extent = extent;
    m_imageInfo.mipLevels = mipLevels;
    m_imageInfo.arrayLayers = arrayLayers;
    m_imageInfo.samples = samples;
    m_imageInfo.tiling = tiling;
    m_imageInfo.usage = imageUsage;
    m_imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    m_imageInfo.initialLayout = initialLayout;

    VK_CHECK(vmaCreateImage(m_allocator, &m_imageInfo, &allocInfo, &m_handle, &m_allocation, nullptr), "failed to create VMA image!");
}

Image::~Image()
{
    vmaDestroyImage(m_allocator, m_handle, m_allocation);
}

void* Image::map() const
{
    void* data;
    VK_CHECK(vmaMapMemory(m_allocator, m_allocation, &data), "failed to map VMA image!");
    return data;
}

void Image::unmap() const
{
    vmaUnmapMemory(m_allocator, m_allocation);
}

ImageView::ImageView(VkDevice device, vk::Image& image, VkImageAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t levelCount, uint32_t baseArrayLayer, uint32_t layerCount, VkImageViewType viewType) : m_device(device), m_img(&image)
{
    VkImageSubresourceRange subRange{};
    subRange.aspectMask = aspectMask;
    subRange.baseMipLevel = baseMipLevel;
    subRange.levelCount = levelCount;
    subRange.baseArrayLayer = baseArrayLayer;
    subRange.layerCount = layerCount;

    m_viewInfo.image = image;
    m_viewInfo.viewType = viewType;
    m_viewInfo.format = image.m_imageInfo.format;
    m_viewInfo.subresourceRange = subRange;

    VK_CHECK(vkCreateImageView(m_device, &m_viewInfo, nullptr, &m_handle), "failed to create image view!");
}

ImageView::~ImageView()
{
    vkDestroyImageView(m_device, m_handle, nullptr);
}

void CommandBuffer::imageMemoryBarrier(Image& img, VkImageAspectFlags aspectMask, VkPipelineStageFlags srcStageMask, VkAccessFlags srcAccessMask, VkPipelineStageFlags dstStageMask, VkAccessFlags dstAccessMask, VkImageLayout newLayout, uint32_t baseMipLevel, uint32_t levelCount, uint32_t baseArrayLayer, uint32_t layerCount) const
{
    VkImageSubresourceRange subRange{};
    subRange.aspectMask = aspectMask;
    subRange.baseArrayLayer = baseArrayLayer;
    subRange.baseMipLevel = baseMipLevel;
    subRange.layerCount = layerCount;
    subRange.levelCount = levelCount;

    VkImageMemoryBarrier imageMemoryBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    imageMemoryBarrier.srcAccessMask = srcAccessMask;
    imageMemoryBarrier.dstAccessMask = dstAccessMask;
    imageMemoryBarrier.oldLayout = img.m_layout;
    imageMemoryBarrier.newLayout = newLayout;
    imageMemoryBarrier.image = img;
    imageMemoryBarrier.subresourceRange = subRange;

    vkCmdPipelineBarrier(m_handle, srcStageMask, dstStageMask, 0u, 0u, nullptr, 0u, nullptr, 1u, &imageMemoryBarrier);

    img.m_layout = newLayout;
}

void CommandBuffer::imageMemoryBarrier(const ImageView& view, VkPipelineStageFlags srcStageMask, VkAccessFlags srcAccessMask, VkPipelineStageFlags dstStageMask, VkAccessFlags dstAccessMask, VkImageLayout newLayout) const
{
    VkImageMemoryBarrier imageMemoryBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    imageMemoryBarrier.srcAccessMask = srcAccessMask;
    imageMemoryBarrier.dstAccessMask = dstAccessMask;
    imageMemoryBarrier.oldLayout = view.m_img->m_layout;
    imageMemoryBarrier.newLayout = newLayout;
    imageMemoryBarrier.image = *view.m_img;
    imageMemoryBarrier.subresourceRange = view.m_viewInfo.subresourceRange;

    vkCmdPipelineBarrier(m_handle, srcStageMask, dstStageMask, 0u, 0u, nullptr, 0u, nullptr, 1u, &imageMemoryBarrier);

    view.m_img->m_layout = newLayout;
}

PipelineLayout::PipelineLayout(VkDevice device, const uint32_t** const shaderBinaries, const size_t* shaderSizes, const VkShaderStageFlags* shaderStages, const size_t shaderCount) : m_device(device)
{
    // TODO different sets
    for (size_t i = 0; i < shaderCount; i++)
    {
        spirv_cross::CompilerGLSL comp(shaderBinaries[i], shaderSizes[i]);
        spirv_cross::ShaderResources resources = comp.get_shader_resources();
        for (auto& u : resources.uniform_buffers)
        {
            VkDescriptorSetLayoutBinding uniformBinding{};
            uniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            // TODO arrays
            uniformBinding.descriptorCount = 1u;
            uniformBinding.stageFlags = shaderStages[i];
            uniformBinding.binding = comp.get_decoration(u.id, spv::DecorationBinding);

            m_bindings.push_back(uniformBinding);
        }

        for (auto& img : resources.sampled_images)
        {
            VkDescriptorSetLayoutBinding imgBinding{};
            imgBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            // TODO arrays
            imgBinding.descriptorCount = 1u;
            imgBinding.stageFlags = shaderStages[i];
            imgBinding.binding = comp.get_decoration(img.id, spv::DecorationBinding);

            m_bindings.push_back(imgBinding);
        }

        for (auto& img : resources.storage_images)
        {
            VkDescriptorSetLayoutBinding imgBinding{};
            imgBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            // TODO arrays
            imgBinding.descriptorCount = 1u;
            imgBinding.stageFlags = shaderStages[i];
            imgBinding.binding = comp.get_decoration(img.id, spv::DecorationBinding);

            m_bindings.push_back(imgBinding);
        }

        for (auto& as : resources.acceleration_structures)
        {
            VkDescriptorSetLayoutBinding asBinding{};
            asBinding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            // TODO arrays
            asBinding.descriptorCount = 1u;
            asBinding.stageFlags = shaderStages[i];
            asBinding.binding = comp.get_decoration(as.id, spv::DecorationBinding);

            m_bindings.push_back(asBinding);
        }
    }

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    descriptorSetLayoutInfo.bindingCount = m_bindings.size();
    descriptorSetLayoutInfo.pBindings = m_bindings.data();

    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &descriptorSetLayoutInfo, nullptr, &m_descriptorSetLayout), "failed to create descriptor set layout!");

    // TODO push constants
    VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layoutInfo.setLayoutCount = 1u;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;

    VK_CHECK(vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_handle), "failed to create pipeline layout!");
}

PipelineLayout::~PipelineLayout()
{
    vkDestroyPipelineLayout(m_device, m_handle, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
}

void DescriptorSet::setUniformBuffer(uint32_t binding, VkBuffer buf, VkDeviceSize range, VkDeviceSize offset) const
{
    VkDescriptorBufferInfo info;
    info.buffer = buf;
    info.range = range;
    info.offset = offset;
    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = m_handle;
    write.dstBinding = binding;
    // TODO arrays
    write.descriptorCount = 1u;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &info;

    vkUpdateDescriptorSets(m_device, 1u, &write, 0u, nullptr);
}

void DescriptorSet::setCombinedImageSampler(uint32_t binding, VkImageView view, VkImageLayout layout, VkSampler sampler) const
{
    VkDescriptorImageInfo info;
    info.imageView = view;
    info.imageLayout = layout;
    info.sampler = sampler;
    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = m_handle;
    write.dstBinding = binding;
    // TODO arrays
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;

    vkUpdateDescriptorSets(m_device, 1u, &write, 0u, nullptr);
}

void DescriptorSet::setImage(uint32_t binding, VkImageView view, VkImageLayout layout) const
{
    VkDescriptorImageInfo info{};
    info.imageView = view;
    info.imageLayout = layout;
    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = m_handle;
    write.dstBinding = binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = &info;

    vkUpdateDescriptorSets(m_device, 1u, &write, 0u, nullptr);
}

void DescriptorSet::setAccelerationStructure(uint32_t binding, VkAccelerationStructureKHR AS) const
{
    VkWriteDescriptorSetAccelerationStructureKHR writeAS{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
    writeAS.accelerationStructureCount = 1u;
    writeAS.pAccelerationStructures = &AS;
    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = m_handle;
    write.dstBinding = binding;
    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    write.pNext = &writeAS;
    
    vkUpdateDescriptorSets(m_device, 1u, &write, 0u, nullptr);
}

DescriptorPool::DescriptorPool(VkDevice device, uint32_t maxSets, const VkDescriptorSetLayoutBinding* bindings, const size_t bindingsCount) : m_device(device)
{
    std::vector<VkDescriptorPoolSize> sizes;
    sizes.reserve(bindingsCount);
    for (size_t i = 0; i < bindingsCount; i++)
    {
        VkDescriptorPoolSize size;
        size.descriptorCount = bindings[i].descriptorCount;
        size.type = bindings[i].descriptorType;
    }

    VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    poolInfo.maxSets = maxSets;
    poolInfo.poolSizeCount = sizes.size();
    poolInfo.pPoolSizes = sizes.data();

    VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_handle), "failed to create descriptor pool!");
}

DescriptorPool::~DescriptorPool()
{
    vkDestroyDescriptorPool(m_device, m_handle, nullptr);
}

std::shared_ptr<DescriptorSet> DescriptorPool::allocateDescriptorSet(VkDescriptorSetLayout layout)
{
    VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    allocInfo.descriptorPool = m_handle;
    allocInfo.descriptorSetCount = 1u;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet set;
    VK_CHECK(vkAllocateDescriptorSets(m_device, nullptr, &set), "failed to allocate descriptor set!");
    return std::make_shared<DescriptorSet>(m_device, set);
}

}
