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
    Buffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags = 0u, VkMemoryPropertyFlags memoryFlags = 0u, VkDeviceSize minAlignment = 0u);
    Buffer(const Buffer&) = delete;

    ~Buffer();

    void* map() const;
    void unmap() const;

    VkBuffer getHandle() const { return m_handle; }

    Buffer& operator=(const Buffer&) = delete;
    operator VkBuffer() const { return m_handle; }

    VkDeviceSize m_size;

private:
    VmaAllocator m_allocator;
    VmaAllocation m_allocation;
    VkBuffer m_handle;
};

class Image
{
public:
    Image(VmaAllocator allocator, VkFormat format, VkExtent3D extent, VkImageUsageFlags imageUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkMemoryPropertyFlags memoryFlags, VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL, uint32_t mipLevels = 1u, uint32_t arrayLayers = 1u, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT, VkImageType imageType = VK_IMAGE_TYPE_2D);
    Image(const Image&) = delete;

    ~Image();

    void* map() const;
    void unmap() const;

    VkImage getHandle() const { return m_handle; };

    Image& operator=(const Image&) = delete;
    operator VkImage() const { return m_handle; };

    VkImageCreateInfo m_imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    VkImageLayout m_layout;

private:
    VmaAllocator m_allocator;
    VmaAllocation m_allocation;
    VkImage m_handle;
};

class ImageView
{
public:
    ImageView(VkDevice device, vk::Image& image, VkImageAspectFlags aspectMask, uint32_t baseMipLevel = 0u, uint32_t levelCount = 1u, uint32_t baseArrayLayer = 0u, uint32_t layerCount = 1u, VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D);
    ImageView(const ImageView&) = delete;

    ~ImageView();

    VkImageView getHandle() const { return m_handle; }

    ImageView& operator=(const ImageView&) = delete;
    operator VkImageView() const { return m_handle; }

    VkImageViewCreateInfo m_viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    Image* m_img;

private:
    VkDevice m_device;
    VkImageView m_handle;
};

class CommandBuffer
{
public:
    CommandBuffer(VkCommandBuffer cmd) : m_handle(cmd) {}

    void imageMemoryBarrier(Image& img, VkImageAspectFlags aspectMask, VkPipelineStageFlags srcStageMask, VkAccessFlags srcAccessMask, VkPipelineStageFlags dstStageMask, VkAccessFlags dstAccessMask, VkImageLayout newLayout, uint32_t baseMipLevel = 0u, uint32_t levelCount = 1u, uint32_t baseArrayLayer = 0u, uint32_t layerCount = 1u) const;

    void imageMemoryBarrier(const ImageView& view, VkPipelineStageFlags srcStageMask, VkAccessFlags srcAccessMask, VkPipelineStageFlags dstStageMask, VkAccessFlags dstAccessMask, VkImageLayout newLayout) const;

    operator VkCommandBuffer() const { return m_handle; }

    VkCommandBuffer m_handle;
};

class RenderContext
{
public:
    RenderContext(GLFWwindow* window);
    RenderContext(const RenderContext&) = delete;

    ~RenderContext();

    VkDevice getDevice() const { return m_device; }
    VkCommandBuffer getCommandBuffer() const { return m_cmdBuf; }
    uint32_t acquireNextSwapchainImage(VkSemaphore acquiredSemaphore) const;
    VkImage getSwapchainImage(uint32_t idx) const { return m_swapchainImages[idx]; }

    void submitToQueue(VkSemaphore waitSemaphore, VkPipelineStageFlags waitStageMask, VkSemaphore signalSemaphore, VkFence fence) const;
    void present(uint32_t swapIdx, VkSemaphore waitSemaphore) const;
    void deviceWaitIdle() const { VK_CHECK(vkDeviceWaitIdle(m_device), "device failed to wait idle!"); }

    std::shared_ptr<Buffer> createBuffer(VkDeviceSize size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags = 0u, VkMemoryPropertyFlags memoryFlags = 0u, VkDeviceSize minAlignment = 0u) const;

    std::shared_ptr<Image> createImage(VkFormat format, VkExtent3D extent, VkImageUsageFlags imageUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkMemoryPropertyFlags memoryFlags, VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL, uint32_t mipLevels = 1u, uint32_t arrayLayers = 1u, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT, VkImageType imageType = VK_IMAGE_TYPE_2D) const;

    std::shared_ptr<ImageView> createImageView(vk::Image& image, VkImageAspectFlags aspectMask, uint32_t baseMipLevel = 0u, uint32_t levelCount = 1u, uint32_t baseArrayLayer = 0u, uint32_t layerCount = 1u, VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D) const;

    void copyStagingBuffer(const Buffer& dst, const Buffer& src, VkDeviceSize size, VkDeviceSize dstOffset = 0u, VkDeviceSize srcOffset = 0u) const;
    void copyStagingImage(Image& dst, Image& src, VkExtent3D extent, VkImageAspectFlags dstAspectMask, VkImageAspectFlags srcAspectMask, VkImageLayout dstFinalLayout, uint32_t dstMipLevel = 0u, uint32_t srcMipLevel = 0u, VkOffset3D dstOffset = { 0, 0, 0 }, VkOffset3D srcOffset = { 0, 0, 0 }, uint32_t dstBaseArrayLayer = 0u, uint32_t srcBaseArrayLayer = 0u, uint32_t dstLayerCount = 1u, uint32_t srcLayerCount = 1u) const;

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