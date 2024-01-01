#pragma once

#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <map>
#include <unordered_set>

#include "vk_mem_alloc.h"

#define LOG(msg) std::cout << (msg) << std::endl
#define LOGW(msg) std::cerr << "[WRN] " << (msg) << std::endl
#define LOGE(msg) std::cerr << "[ERR] " << (msg) << std::endl

namespace vk
{

class Instance
{
public:
    Instance();
    Instance(const Instance&) = delete;

    ~Instance();

    bool create();
    void destroy() { vkDestroyInstance(m_handle, nullptr); }
    VkInstance getHandle() const { return m_handle; }

    Instance& operator=(const Instance&) = delete;
    inline operator VkInstance() const { return m_handle; };

    VkApplicationInfo m_appInfo{};
    VkInstanceCreateInfo m_createInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    std::vector<const char*> m_enabledLayers;
    std::vector<const char*> m_enabledExtensions;

private:
    VkInstance m_handle = VK_NULL_HANDLE;
};

class Device
{
public:
    Device(Instance* instance);
    Device(const Device&) = delete;

    ~Device();

    bool create();
    void destroy() { vkDestroyDevice(m_handle, nullptr); }
    inline VkDevice getHandle() const { return m_handle; }

    VkQueue getQueue(VkQueueFlags flags, uint32_t idx) const;
    bool submitToQueue(VkQueue queue, VkCommandBuffer cmdBuf, VkSemaphore waitSemaphore, VkPipelineStageFlags waitStageMask, VkSemaphore signalSemaphore, VkFence fence) const;
    bool waitIdle() const;

    Device& operator=(const Device&) = delete;
    inline operator VkDevice() const { return m_handle; }

    struct QueueRequirements
    {
        VkQueueFlags flags;
        uint32_t count;
    };

    Instance* m_instance;
    VkPhysicalDeviceType m_physicalDeviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    VkPhysicalDevice m_physicalDevice;
    VkPhysicalDeviceFeatures m_enabledFeatures{};
    std::vector<const char*> m_enabledExtensions;
    std::vector<QueueRequirements> m_queueRequirements;
    VkDeviceCreateInfo m_createInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };

private:
    VkDevice m_handle = VK_NULL_HANDLE;
    std::map<VkQueueFlags, uint32_t> m_queueFlagsToQueueFamily;
};

class Swapchain
{
public:
    Swapchain(Device* device);
    Swapchain(const Swapchain&) = delete;

    ~Swapchain();

    bool create(VkSurfaceKHR surface, VkImageUsageFlags usage);
    void destroy() { vkDestroySwapchainKHR(*m_device, m_handle, nullptr); }
    inline VkSwapchainKHR getHandle() const { return m_handle; }

    bool acquireNextImage(uint32_t* idx, VkSemaphore acquiredSemaphore) const;
    bool present(VkQueue queue, uint32_t idx, VkSemaphore waitSemaphore) const;

    Swapchain& operator=(const Swapchain&) = delete;
    inline operator VkSwapchainKHR() const { return m_handle; }

    Device* m_device;
    VkSwapchainCreateInfoKHR m_createInfo{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    std::vector<VkImage> m_images;

private:
    VkSwapchainKHR m_handle = VK_NULL_HANDLE;
};

class Buffer
{
public:
    Buffer(VmaAllocator allocator);
    Buffer(const Buffer&) = delete;

    ~Buffer();

    bool create(VkDeviceSize size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocationFlags, VkMemoryPropertyFlags memoryFlags, VkDeviceSize minAlignment = 0u);
    void destroy() { vmaDestroyBuffer(m_allocator, m_handle, m_allocation); }
    inline VkBuffer getHandle() const { return m_handle; }

    bool map(void** data) const;
    void unmap() const { vmaUnmapMemory(m_allocator, m_allocation); }

    Buffer& operator=(const VkBuffer&) = delete;
    inline operator VkBuffer() const { return m_handle; }

    VkBufferCreateInfo m_createInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VmaAllocationCreateInfo m_allocationInfo{};

private:
    VmaAllocator m_allocator;
    VmaAllocation m_allocation = nullptr;
    VkBuffer m_handle = VK_NULL_HANDLE;
};

class Image
{
public:
    Image(VmaAllocator allocator);
    Image(const Image&) = delete;

    ~Image();

    bool create(VkExtent3D extent, VkImageTiling tiling, VkImageLayout initialLayout, VkImageUsageFlags imageUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocationFlags, VkMemoryPropertyFlags memoryFlags);
    void destroy() { vmaDestroyImage(m_allocator, m_handle, m_allocation); }
    inline VkImage getHandle() const { return m_handle; }

    bool map(void** data) const;
    void unmap() const { vmaUnmapMemory(m_allocator, m_allocation); }

    Image& operator=(const Image&) = delete;
    inline operator VkImage() const { return m_handle; }

    VkImageCreateInfo m_createInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    VmaAllocationCreateInfo m_allocationInfo{};
    VkImageLayout m_layout;

private:
    VmaAllocator m_allocator;
    VmaAllocation m_allocation = nullptr;
    VkImage m_handle = VK_NULL_HANDLE;
};

class ImageView
{
public:
    ImageView(Device* device, Image& img);
    ImageView(const ImageView&) = delete;

    ~ImageView();

    bool create(VkImageAspectFlags aspectMask);
    void destroy() { vkDestroyImageView(*m_device, m_handle, nullptr); }
    inline VkImageView getHandle() const { return m_handle; }

    VkImageView& operator=(const ImageView&) = delete;
    inline operator VkImageView() const { return m_handle; }

    Image& m_img;
    VkImageViewCreateInfo m_createInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };

private:
    Device* m_device;
    VkImageView m_handle = VK_NULL_HANDLE;
};

class Shader
{
public:
    Shader(Device* device) : m_device(device) {};
    Shader(const Shader&) = delete;

    ~Shader();

    bool create(const std::string& spirvFilepath, VkShaderStageFlagBits stage);
    void destroy() { vkDestroyShaderModule(*m_device, m_module, nullptr); }

    Shader& operator=(const Shader&) = delete;

    std::vector<uint32_t> m_code;
    VkPipelineShaderStageCreateInfo m_shaderStageInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };

private:
    Device* m_device;
    VkShaderModule m_module = VK_NULL_HANDLE;
};

class PipelineLayout
{
public:
    PipelineLayout(Device* device) : m_device(device) {}
    PipelineLayout(const PipelineLayout&) = delete;

    ~PipelineLayout();

    bool create(const std::unordered_set<Shader>& shaders);
    void destroy();
    inline VkPipelineLayout getHandle() const { return m_handle; }

    PipelineLayout& operator=(const PipelineLayout&) = delete;
    inline operator VkPipelineLayout() const { return m_handle; }

private:
    Device* m_device;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_handle = VK_NULL_HANDLE;
};

class GraphicsPipeline
{
public:
    GraphicsPipeline(Device* device);
    GraphicsPipeline(const GraphicsPipeline&) = delete;

    ~GraphicsPipeline();

    bool create(const std::unordered_set<Shader>& shaders, std::shared_ptr<PipelineLayout> layout, uint32_t width, uint32_t height);
    bool create(const std::unordered_set<Shader>& shaders, uint32_t width, uint32_t height);
    void destroy() { vkDestroyPipeline(*m_device, m_handle, nullptr); }
    inline VkPipeline getHandle() const { return m_handle; }

    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
    inline operator VkPipeline() const { return m_handle; }

    std::shared_ptr<PipelineLayout> m_layout;
    std::vector<VkVertexInputBindingDescription> m_vertexBindings;
    std::vector<VkVertexInputAttributeDescription> m_vertexAttributes;
    std::vector<VkPipelineColorBlendAttachmentState> m_colorBlendAttachmentStates;

    VkPipelineInputAssemblyStateCreateInfo m_inputAssemblyInfo{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    VkViewport m_viewport{};
    VkRect2D m_scissor{};
    VkPipelineRasterizationStateCreateInfo m_rasterizerInfo{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    VkPipelineMultisampleStateCreateInfo m_multisampleInfo{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    VkPipelineDepthStencilStateCreateInfo m_depthInfo{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

    VkGraphicsPipelineCreateInfo m_createInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };

private:
    bool create(const std::unordered_set<Shader>& shaders);

    Device* m_device;
    VkPipeline m_handle = VK_NULL_HANDLE;
};

class CommandBuffer
{
public:
    CommandBuffer(Device* device, VkCommandPool cmdPool) : m_device(device), m_cmdPool(cmdPool) {}
    CommandBuffer(const VkCommandPool&) = delete;

    ~CommandBuffer();

    bool create();
    void destroy() { vkFreeCommandBuffers(*m_device, m_cmdPool, 1u, &m_handle); }
    inline VkCommandBuffer getHandle() const { return m_handle; }

    void imageMemoryBarrier(Image& img, VkImageAspectFlags aspectMask, VkPipelineStageFlags srcStageMask, VkAccessFlags srcAccessMask, VkPipelineStageFlags dstStageMask, VkAccessFlags dstAccessMask, VkImageLayout newLayout);

    CommandBuffer& operator=(const CommandBuffer&) = delete;
    inline operator VkCommandBuffer() const { return m_handle; }

private:
    Device* m_device;
    VkCommandPool m_cmdPool;
    VkCommandBuffer m_handle = VK_NULL_HANDLE;
};

//class RenderContext
//{
//public:
//    RenderContext(GLFWwindow* window);
//    RenderContext(const RenderContext&) = delete;
//
//    ~RenderContext();
//
//    VkDevice getDevice() const { return m_device; }
//    CommandBuffer getCommandBuffer() const { return CommandBuffer(m_cmdBuf); }
//    uint32_t acquireNextSwapchainImage(VkSemaphore acquiredSemaphore) const;
//    VkImage getSwapchainImage(uint32_t idx) const { return m_swapchainImages[idx]; }
//
//    void submitToQueue(VkSemaphore waitSemaphore, VkPipelineStageFlags waitStageMask, VkSemaphore signalSemaphore, VkFence fence) const;
//    void present(uint32_t swapIdx, VkSemaphore waitSemaphore) const;
//    void deviceWaitIdle() const { VK_CHECK(vkDeviceWaitIdle(m_device), "device failed to wait idle!"); }
//
//    std::shared_ptr<Buffer> createBuffer(VkDeviceSize size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags = 0u, VkMemoryPropertyFlags memoryFlags = 0u, VkDeviceSize minAlignment = 0u) const;
//
//    std::shared_ptr<Image> createImage(VkFormat format, VkExtent3D extent, VkImageUsageFlags imageUsage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags, VkMemoryPropertyFlags memoryFlags, VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL, uint32_t mipLevels = 1u, uint32_t arrayLayers = 1u, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT, VkImageType imageType = VK_IMAGE_TYPE_2D) const;
//
//    std::shared_ptr<ImageView> createImageView(vk::Image& image, VkImageAspectFlags aspectMask, uint32_t baseMipLevel = 0u, uint32_t levelCount = 1u, uint32_t baseArrayLayer = 0u, uint32_t layerCount = 1u, VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D) const;
//
//    void copyStagingBuffer(const Buffer& dst, const Buffer& src, VkDeviceSize size, VkDeviceSize dstOffset = 0u, VkDeviceSize srcOffset = 0u) const;
//    void copyStagingImage(Image& dst, Image& src, VkExtent3D extent, VkImageAspectFlags dstAspectMask, VkImageAspectFlags srcAspectMask, VkImageLayout dstFinalLayout, uint32_t dstMipLevel = 0u, uint32_t srcMipLevel = 0u, VkOffset3D dstOffset = { 0, 0, 0 }, VkOffset3D srcOffset = { 0, 0, 0 }, uint32_t dstBaseArrayLayer = 0u, uint32_t srcBaseArrayLayer = 0u, uint32_t dstLayerCount = 1u, uint32_t srcLayerCount = 1u) const;
//
//    void buildAS(VkAccelerationStructureBuildGeometryInfoKHR buildInfo, VkAccelerationStructureBuildRangeInfoKHR* rangeInfo) const;
//
//    RenderContext& operator=(const RenderContext&) = delete;
//
//    VkExtent2D m_extent;
//    VkPhysicalDeviceAccelerationStructurePropertiesKHR m_ASProperties;
//
//private:
//    VkInstance m_instance;
//    VkSurfaceKHR m_surface;
//    VkPhysicalDevice m_physicalDevice;
//    uint32_t m_queueFamilyIdx;
//    VkDevice m_device;
//    VkQueue m_queue;
//    VkCommandPool m_cmdPool;
//    VkCommandPool m_cmdPoolTransient;
//    VkCommandBuffer m_cmdBuf;
//    VkSwapchainKHR m_swapchain;
//    std::vector<VkImage> m_swapchainImages;
//    VmaAllocator m_allocator;
//
//    void createInstance();
//    void createSurface(GLFWwindow* window);
//    void getPhysicalDevice();
//    void chooseGctPresentQueue();
//    void createDeviceAndQueue();
//    void createCommandBuffer();
//    void createSwapchain();
//};

}