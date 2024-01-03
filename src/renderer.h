#pragma once

#include "vk_graphics.h"

class Renderer
{
public:
    Renderer(vk::Device* gpu, GLFWwindow* window, uint32_t width, uint32_t height) : m_gpu(gpu), m_window(window), m_width(width), m_height(height) {}
    Renderer(const Renderer&) = delete;

    ~Renderer();

    bool init();
    bool render();

    Renderer& operator=(const Renderer&) = delete;

private:
    vk::Device* m_gpu;
    GLFWwindow* m_window;
    uint32_t m_width;
    uint32_t m_height;
    std::unique_ptr<vk::Swapchain> m_swapchain;
    std::unique_ptr<vk::GraphicsPipeline> m_gfxPipe;
    VkQueue m_gct = VK_NULL_HANDLE;
    VkCommandPool m_cmdPool = VK_NULL_HANDLE;
    std::unique_ptr<vk::CommandBuffer> m_cmdBuf;

    VkSemaphore m_imageAcquired = VK_NULL_HANDLE;
    VkSemaphore m_renderDone = VK_NULL_HANDLE;
    VkFence m_renderFence = VK_NULL_HANDLE;

    std::vector<VkImageView> m_swapchainViews;
};