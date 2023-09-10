#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

class Allocator {
public:
	Allocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device) {
		VmaAllocatorCreateInfo vmaAllocatorInfo{};
		vmaAllocatorInfo.instance = instance;
		vmaAllocatorInfo.physicalDevice = physicalDevice;
		vmaAllocatorInfo.device = device;
		// TODO check return value
		vmaCreateAllocator(&vmaAllocatorInfo, &m_allocator);
	}
	Allocator(const Allocator&) = delete;

	~Allocator() { vmaDestroyAllocator(m_allocator); }

	operator VmaAllocator() const { return m_allocator; }
private:
	VmaAllocator m_allocator;
};

class AllocatedBuffer {
public:
	AllocatedBuffer(VmaAllocator allocator, size_t size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage) : m_allocator(allocator) {
		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = bufferUsage;

		VmaAllocationCreateInfo vmaAllocInfo{};
		vmaAllocInfo.usage = memoryUsage;
		// TODO check return value
		vmaCreateBuffer(m_allocator, &bufferInfo, &vmaAllocInfo, &m_buffer, &m_allocation, nullptr);
	}
	AllocatedBuffer(const AllocatedBuffer&) = delete;

	~AllocatedBuffer() { vmaDestroyBuffer(m_allocator, m_buffer, m_allocation); }

	void* map() const {
		void* data;
		// TODO check return value
		vmaMapMemory(m_allocator, m_allocation, &data);
		return data;
	}
	void unmap() const { vmaUnmapMemory(m_allocator, m_allocation); }

	operator VkBuffer() const { return m_buffer; }
private:
	VmaAllocator m_allocator;
	VkBuffer m_buffer;
	VmaAllocation m_allocation;
};

class AllocatedImage {
public:
	AllocatedImage(VmaAllocator allocator, VkFormat format, VkExtent3D extent, VkImageUsageFlags imageUsage, VmaMemoryUsage memoryUsage) : m_allocator(allocator), m_format(format), m_extent(extent) {
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = m_format;
		imageInfo.extent = m_extent;
		imageInfo.arrayLayers = 1;
		imageInfo.mipLevels = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = imageUsage;

		VmaAllocationCreateInfo vmaAllocInfo{};
		vmaAllocInfo.usage = memoryUsage;
		// TODO check return value
		vmaCreateImage(m_allocator, &imageInfo, &vmaAllocInfo, &m_image, &m_allocation, nullptr);
	}
	AllocatedImage(const AllocatedImage&) = delete;

	~AllocatedImage() { vmaDestroyImage(m_allocator, m_image, m_allocation); }

	VkFormat getFormat() const { return m_format; }
	VkExtent3D getExtent() const { return m_extent; }

	operator VkImage() const { return m_image; }
private:
	VmaAllocator m_allocator;
	VkImage m_image;
	VmaAllocation m_allocation;

	VkFormat m_format;
	VkExtent3D m_extent;
};