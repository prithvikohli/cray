#include "vk_graphics.hpp"

#ifndef NDEBUG
#define ENABLED_LAYER_COUNT 1u
static const char* ENABLED_LAYER_NAMES[ENABLED_LAYER_COUNT] = { "VK_LAYER_KHRONOS_validation" };
#else
#define ENABLED_LAYER_COUNT 0u
static const chair** ENABLED_LAYER_NAMES = nullptr;
#endif

#define ENABLED_DEVICE_EXTENSION_COUNT 4u
static const char* ENABLED_DEVICE_EXTENSION_NAMES[ENABLED_DEVICE_EXTENSION_COUNT] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME };

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
    createSwapchainViews();
}

RenderContext::~RenderContext()
{
    for (VkImageView view : m_swapchainViews)
        vkDestroyImageView(m_device, view, nullptr);
    vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    vkDestroyCommandPool(m_device, m_cmdPool, nullptr);
    vkDestroyDevice(m_device, nullptr);
    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyInstance(m_instance, nullptr);
}

void RenderContext::createInstance()
{
    uint32_t apiVersion;
    VK_CHECK(vkEnumerateInstanceVersion(&apiVersion), "failed to get Vulkan API version!");

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.apiVersion = apiVersion;
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
    vkEnumeratePhysicalDevices(m_instance, nullptr, physicalDevices.data());

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
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, nullptr, queueFamilyProps.data());

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
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_STORAGE_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainInfo.clipped = VK_TRUE;

    VK_CHECK(vkCreateSwapchainKHR(m_device, &swapchainInfo, nullptr, &m_swapchain), "failed to create swapchain!");
}

void RenderContext::createSwapchainViews()
{
    uint32_t swapchainImageCount;
    VK_CHECK(vkGetSwapchainImagesKHR(m_device, m_swapchain, &swapchainImageCount, nullptr), "failed to get swapchain images!");
    std::vector<VkImage> swapchainImages(swapchainImageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, nullptr, swapchainImages.data());

    m_swapchainViews.reserve(swapchainImageCount);
    for (VkImage img : swapchainImages)
    {
        VkImageSubresourceRange subRange{};
        subRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subRange.layerCount = 1u;
        subRange.levelCount = 1u;
        subRange.baseArrayLayer = 0u;
        subRange.baseMipLevel = 0u;

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = img;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
        viewInfo.subresourceRange = subRange;

        VkImageView view;
        VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &view), "failed to create swapchain image views!");
        m_swapchainViews.push_back(view);
    }
}

}
