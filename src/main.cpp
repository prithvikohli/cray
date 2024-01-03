#include "renderer.h"

// TODO make configurable
#define WINDOW_WIDTH 2560
#define WINDOW_HEIGHT 1440

int main()
{
    int res = glfwInit();
    if (res == GLFW_FALSE)
    {
        LOGE("Failed to initialize GLFW.");
        return 1;
    }
    uint32_t glfwExtensionCount;
    glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (glfwExtensionCount == 0)
    {
        LOGE("Failed to get required GLFW instance extensions.");
        return 1;
    }
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    vk::Instance instance;
    instance.m_enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
    for (uint32_t i = 0; i < glfwExtensionCount; i++)
        instance.m_enabledExtensions.push_back(glfwExtensions[i]);

    if (!instance.create())
    {
        LOGE("Failed to create Vulkan instance.");
        return 1;
    }

    vk::Device gpu(&instance);
    VkPhysicalDeviceSynchronization2Features sync2Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES };
    sync2Features.synchronization2 = VK_TRUE;
    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
    dynamicRenderFeatures.dynamicRendering = VK_TRUE;
    dynamicRenderFeatures.pNext = &sync2Features;
    gpu.m_enabledFeatures.pNext = &dynamicRenderFeatures;

    gpu.m_queueRequirements.push_back({ VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT, 1u });
    gpu.m_enabledExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    gpu.m_enabledExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    gpu.m_enabledExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    gpu.m_enabledExtensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    gpu.m_enabledExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    gpu.m_enabledExtensions.push_back(VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME);

    if (!gpu.create())
    {
        LOGE("Failed to create Vulkan device.");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "cray", nullptr, nullptr);
    if (!window)
    {
        LOGE("Failed to create GLFW window.");
        return 1;
    }

    Renderer renderer(&gpu, window, WINDOW_WIDTH, WINDOW_HEIGHT);
    if (!renderer.init())
    {
        LOGE("Failed to initialize renderer!");
        return 1;
    }

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        if (!renderer.render())
        {
            LOGE("Failed to render frame.");
            break;
        }
    }
    gpu.waitIdle();

    glfwDestroyWindow(window);
    glfwTerminate();
}