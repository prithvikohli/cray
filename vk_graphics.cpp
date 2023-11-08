#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include "vk_graphics.h"

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
    //createSwapchainViews();

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.device = m_device;
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.instance = m_instance;

    VK_CHECK(vmaCreateAllocator(&allocatorInfo, &m_allocator), "failed to create VMA allocator!");
}

RenderContext::~RenderContext()
{
    vmaDestroyAllocator(m_allocator);

    //for (VkImageView view : m_swapchainViews)
    //    vkDestroyImageView(m_device, view, nullptr);
    vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    vkDestroyCommandPool(m_device, m_cmdPool, nullptr);
    vkDestroyDevice(m_device, nullptr);
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyInstance(m_instance, nullptr);
}

void RenderContext::createInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.apiVersion = VK_MAKE_API_VERSION(1, 2, 0, 0);
    appInfo.applicationVersion = 0u;
    appInfo.pApplicationName = "cray";

    uint32_t glfwExtensionCount;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (glfwExtensionCount == 0)
        throw std::runtime_error("failed to get required GLFW extensions!");

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
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
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            physicalDeviceIdx = i;
            break;
        }
    }

    if (physicalDeviceIdx == -1)
        throw std::runtime_error("failed to find appropriate GPU!");

    m_physicalDevice = physicalDevices[physicalDeviceIdx];
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
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = m_queueFamilyIdx;
    queueInfo.queueCount = 1u;
    queueInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1u;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = ENABLED_DEVICE_EXTENSION_COUNT;
    deviceInfo.ppEnabledExtensionNames = ENABLED_DEVICE_EXTENSION_NAMES;

    VK_CHECK(vkCreateDevice(m_physicalDevice, &deviceInfo, nullptr, &m_device), "failed to create device!");
    vkGetDeviceQueue(m_device, m_queueFamilyIdx, 0u, &m_queue);
}

void RenderContext::createCommandBuffer()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_queueFamilyIdx;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VK_CHECK(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_cmdPool), "failed to create command pool!");

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_cmdPool;
    allocInfo.commandBufferCount = 1u;

    VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &m_cmdBuf), "failed to allocate command buffer!");
}

void RenderContext::createSwapchain()
{
    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &surfaceCapabilities), "failed to get physical device surface capabilities!");
    m_extent = surfaceCapabilities.currentExtent;

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
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

//void RenderContext::createSwapchainViews()
//{
//    uint32_t swapchainImageCount;
//    VK_CHECK(vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount, nullptr), "failed to get swapchain images!");
//    m_swapchainImages.resize(swapchainImageCount);
//    vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount, m_swapchainImages.data());
//
//    m_swapchainViews.reserve(swapchainImageCount);
//    for (VkImage img : m_swapchainImages)
//    {
//        VkImageSubresourceRange subRange{};
//        subRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//        subRange.layerCount = 1u;
//        subRange.levelCount = 1u;
//        subRange.baseArrayLayer = 0u;
//        subRange.baseMipLevel = 0u;
//
//        VkImageViewCreateInfo viewInfo{};
//        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
//        viewInfo.image = img;
//        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
//        viewInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
//        viewInfo.subresourceRange = subRange;
//
//        VkImageView view;
//        VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &view), "failed to create swapchain image views!");
//        m_swapchainViews.push_back(view);
//    }
//}

void RenderContext::acquireNextSwapchainImage(uint32_t* swapIdx, VkSemaphore acquiredSemaphore) const
{
    VK_CHECK(vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX, acquiredSemaphore, VK_NULL_HANDLE, swapIdx), "failed to acquire swapchain image!");
}

void RenderContext::submitToQueue(VkSubmitInfo submitInfo, VkFence fence) const
{
    VK_CHECK(vkQueueSubmit(m_queue, 1u, &submitInfo, fence), "failed to submit to queue!");
}

void RenderContext::present(uint32_t swapIdx, VkSemaphore waitSemaphore) const
{
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1u;
    presentInfo.pWaitSemaphores = &waitSemaphore;
    presentInfo.swapchainCount = 1u;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &swapIdx;

    VK_CHECK(vkQueuePresentKHR(m_queue, &presentInfo), "failed to present swapchain!");
}

std::shared_ptr<Image> RenderContext::createImage(VkImageCreateInfo imageInfo, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkMemoryPropertyFlags memoryFlags) const
{
    std::shared_ptr<Image> img = std::make_shared<Image>(m_allocator, imageInfo, memoryUsage, allocFlags, memoryFlags);
    return img;
}

std::shared_ptr<ImageView> RenderContext::createImageView(const vk::Image& image, VkImageViewType viewType, VkImageSubresourceRange subRange) const
{
    std::shared_ptr<ImageView> view = std::make_shared<ImageView>(m_device, image, viewType, subRange);
    return view;
}

Buffer::Buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkMemoryPropertyFlags memoryFlags) : m_allocator(allocator), m_size(size)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = m_size;
    bufferInfo.usage = bufferUsage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;
    allocInfo.flags = allocFlags;
    allocInfo.requiredFlags = memoryFlags;

    VK_CHECK(vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &m_handle, &m_allocation, nullptr), "failed to create VMA buffer!");
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

VkBuffer Buffer::getHandle() const
{
    return m_handle;
}

Buffer::operator VkBuffer() const
{
    return m_handle;
}

Image::Image(VmaAllocator allocator, VkImageCreateInfo imageInfo, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkMemoryPropertyFlags memoryFlags) : m_allocator(allocator), m_imageInfo(imageInfo)
{
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;
    allocInfo.flags = allocFlags;
    allocInfo.requiredFlags = memoryFlags;

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

VkImage Image::getHandle() const
{
    return m_handle;
}

Image::operator VkImage() const
{
    return m_handle;
}

ImageView::ImageView(VkDevice device, const vk::Image& image, VkImageViewType viewType, VkImageSubresourceRange subRange) : m_device(device)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = viewType;
    viewInfo.format = image.m_imageInfo.format;
    viewInfo.subresourceRange = subRange;

    VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &m_handle), "failed to create image view!");
}

ImageView::~ImageView()
{
    vkDestroyImageView(m_device, m_handle, nullptr);
}

VkImageView ImageView::getHandle() const
{
    return m_handle;
}

ImageView::operator VkImageView() const
{
    return m_handle;
}

}
