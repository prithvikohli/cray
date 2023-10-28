#pragma once

#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <exception>

#include "vk_mem_alloc.h"

#define LOG(msg) std::cout << msg << std::endl
#define VK_CHECK(func, err) if (func != VK_SUCCESS) throw std::runtime_error(err)

namespace vk
{

class RenderContext
{
public:
    RenderContext(GLFWwindow* window);
    RenderContext(const RenderContext&) = delete;

    ~RenderContext();

    VkDevice getDevice() const { return m_device; }

    RenderContext& operator=(const RenderContext&) = delete;

    VkExtent2D m_extent;

private:
    VkInstance m_instance;
    VkSurfaceKHR m_surface;
    VkPhysicalDevice m_physicalDevice;
    uint32_t m_queueFamilyIdx;
    VkDevice m_device;
    VkQueue m_queue;
    VkCommandPool m_cmdPool;
    VkCommandBuffer m_cmdBuf;
    VkSwapchainKHR m_swapchain;
    std::vector<VkImageView> m_swapchainViews;
    VmaAllocator m_allocator;

    void createInstance();
    void createSurface(GLFWwindow* window);
    void getPhysicalDevice();
    void chooseGctPresentQueue();
    void createDeviceAndQueue();
    void createCommandBuffer();
    void createSwapchain();
    void createSwapchainViews();
};

class Buffer
{
public:
    Buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage);
    Buffer(const Buffer&) = delete;

    ~Buffer();

    Buffer& operator=(const Buffer&) = delete;

private:
    VkBuffer m_handle;
};

}