#pragma once

#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <exception>

#include "vk_mem_alloc.h"

#define LOG(msg) std::cout << msg << std::endl
#define LOGE(msg) std::cerr << msg << std::endl
#define VK_CHECK(func, err) if (func != VK_SUCCESS) throw std::runtime_error(err)

#define ARRAY_LENGTH(arr) (sizeof(arr) / sizeof(*arr))

namespace vk
{

class Buffer
{
public:
    Buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkMemoryPropertyFlags memoryFlags);
    Buffer(const Buffer&) = delete;

    ~Buffer();

    void* map() const;
    void unmap() const;

    VkBuffer getHandle() const;

    Buffer& operator=(const Buffer&) = delete;
    operator VkBuffer() const;

    VkDeviceSize m_size;

private:
    VmaAllocator m_allocator;
    VmaAllocation m_allocation;
    VkBuffer m_handle;
};

class AlignedBuffer
{
public:
    AlignedBuffer(VmaAllocator allocator, VkDeviceSize size, VkDeviceSize minAlignment, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkMemoryPropertyFlags memoryFlags);
    AlignedBuffer(const Buffer&) = delete;

    ~AlignedBuffer();

    void* map() const;
    void unmap() const;

    VkBuffer getHandle() const;

    AlignedBuffer& operator=(const Buffer&) = delete;
    operator VkBuffer() const;

    VkDeviceSize m_size;

private:
    VmaAllocator m_allocator;
    VmaAllocation m_allocation;
    VkBuffer m_handle;
};

class Image
{
public:
    Image(VmaAllocator allocator, VkImageCreateInfo imageInfo, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkMemoryPropertyFlags memoryFlags);
    Image(const Image&) = delete;

    ~Image();

    void* map() const;
    void unmap() const;

    VkImage getHandle() const;

    Image& operator=(const Image&) = delete;
    operator VkImage() const;

    VkImageCreateInfo m_imageInfo;

private:
    VmaAllocator m_allocator;
    VmaAllocation m_allocation;
    VkImage m_handle;
};

class ImageView
{
public:
    ImageView(VkDevice device, const vk::Image& image, VkImageViewType viewType, VkImageSubresourceRange subRange);
    ImageView(const ImageView&) = delete;

    ~ImageView();

    VkImageView getHandle() const;

    ImageView& operator=(const ImageView&) = delete;
    operator VkImageView() const;

private:
    VkDevice m_device;
    VkImageView m_handle;
};

class RenderContext
{
public:
    RenderContext(GLFWwindow* window);
    RenderContext(const RenderContext&) = delete;

    ~RenderContext();

    VkDevice getDevice() const { return m_device; }
    VkCommandBuffer getCommandBuffer() const { return m_cmdBuf; }
    void acquireNextSwapchainImage(uint32_t* swapIdx, VkSemaphore acquiredSemaphore) const;
    VkImage getSwapchainImage(uint32_t idx) const { return m_swapchainImages[idx]; }

    void submitToQueue(VkSubmitInfo submitInfo, VkFence fence) const;
    void present(uint32_t swapIdx, VkSemaphore waitSemaphore) const;
    void waitIdle() const { VK_CHECK(vkDeviceWaitIdle(m_device), "device failed to wait idle!"); }
    
    std::shared_ptr<Buffer> createBuffer(VkDeviceSize size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkMemoryPropertyFlags memoryFlags) const;
    std::shared_ptr<AlignedBuffer> createAlignedBuffer(VkDeviceSize size, VkDeviceSize minAlignment, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkMemoryPropertyFlags memoryFlags) const;
    std::shared_ptr<Image> createImage(VkImageCreateInfo imageInfo, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkMemoryPropertyFlags memoryFlags) const;
    std::shared_ptr<ImageView> createImageView(const vk::Image& image, VkImageViewType viewType, VkImageSubresourceRange subRange) const;

    void copyBuffer(const Buffer& src, const Buffer& dst) const;
    void copyImage(const Image& src, const Image& dst) const;

    void buildAS(VkAccelerationStructureBuildGeometryInfoKHR buildInfo, VkAccelerationStructureBuildRangeInfoKHR* rangeInfo) const;

    RenderContext& operator=(const RenderContext&) = delete;

    VkExtent2D m_extent;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR m_ASProperties;

private:
    VkInstance m_instance;
    VkSurfaceKHR m_surface;
    VkPhysicalDevice m_physicalDevice;
    uint32_t m_queueFamilyIdx;
    VkDevice m_device;
    VkQueue m_queue;
    VkCommandPool m_cmdPool;
    VkCommandPool m_cmdPoolTransient;
    VkCommandBuffer m_cmdBuf;
    VkSwapchainKHR m_swapchain;
    std::vector<VkImage> m_swapchainImages;
    VmaAllocator m_allocator;

    void createInstance();
    void createSurface(GLFWwindow* window);
    void getPhysicalDevice();
    void chooseGctPresentQueue();
    void createDeviceAndQueue();
    void createCommandBuffer();
    void createSwapchain();
};

}