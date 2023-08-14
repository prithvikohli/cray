#include <vulkan/vulkan_raii.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <fstream>

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

static const int WINDOW_WIDTH = 1920;
static const int WINDOW_HEIGHT = 1080;

class Allocator {
public:
	Allocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device) {
		VmaAllocatorCreateInfo vmaAllocatorInfo{};
		vmaAllocatorInfo.instance = instance;
		vmaAllocatorInfo.physicalDevice = physicalDevice;
		vmaAllocatorInfo.device = device;
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
		vmaCreateBuffer(m_allocator, &bufferInfo, &vmaAllocInfo, &m_buffer, &m_allocation, nullptr);
	}
	AllocatedBuffer(const AllocatedBuffer&) = delete;

	~AllocatedBuffer() { vmaDestroyBuffer(m_allocator, m_buffer, m_allocation); }

	void* map() const {
		void* data;
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

struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
};

struct CameraUniforms {
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 proj;
};

std::vector<uint32_t> readShader(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open())
		throw std::runtime_error("failed to open file \"" + filename + "\"!");
	size_t fileSize = file.tellg();
	std::vector<uint32_t> buffer(fileSize / 4);
	file.seekg(0);
	file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
	file.close();
	return buffer;
}

int main() {
	// initialise GLFW
	/////////////////////////////////////////////////////////////////////////////////////////////
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "cray", nullptr, nullptr);

	// setup instance extensions and layers
	/////////////////////////////////////////////////////////////////////////////////////////////
	uint32_t glfwExtensionCount;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	std::vector<const char*> instanceExtensions;
	instanceExtensions.reserve(glfwExtensionCount);
	for (uint32_t i = 0; i < glfwExtensionCount; i++)
		instanceExtensions.push_back(glfwExtensions[i]);

	std::array<const char*, 1> enabledLayers = { "VK_LAYER_KHRONOS_validation" };

	// create Vulkan instance
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::raii::Context context;
	const uint32_t apiVersion = context.enumerateInstanceVersion();
	vk::ApplicationInfo appInfo("cray", 1, nullptr, 0, apiVersion);
	vk::InstanceCreateInfo instanceInfo({}, &appInfo, enabledLayers, instanceExtensions);
	vk::raii::Instance instance(context, instanceInfo);

	// create surface
	/////////////////////////////////////////////////////////////////////////////////////////////
	VkSurfaceKHR surf;
	glfwCreateWindowSurface(*instance, window, nullptr, &surf);
	vk::raii::SurfaceKHR surface(instance, surf);

	// create physical device
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::raii::PhysicalDevices physicalDevices(instance);
	size_t physicalDeviceIndex = 0;
	for (size_t i = 0; i < physicalDevices.size(); i++) {
		if (physicalDevices[i].getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
			physicalDeviceIndex = i;
			break;
		}
	}
	vk::raii::PhysicalDevice physicalDevice(std::move(physicalDevices[physicalDeviceIndex]));

	// choose GCT queue
	/////////////////////////////////////////////////////////////////////////////////////////////
	uint32_t queueFamilyIndex = 0;
	std::vector<vk::QueueFamilyProperties> qfps = physicalDevice.getQueueFamilyProperties();
	for (size_t i = 0; i < qfps.size(); i++) {
		vk::QueueFlags& flags = qfps[i].queueFlags;
		if ((flags & vk::QueueFlagBits::eCompute) && (flags & vk::QueueFlagBits::eGraphics) && (flags & vk::QueueFlagBits::eTransfer)) {
			queueFamilyIndex = i;
			break;
		}
	}

	// create device and queue
	/////////////////////////////////////////////////////////////////////////////////////////////
	const float queuePriority = 0.0f;
	vk::DeviceQueueCreateInfo queueInfo({}, queueFamilyIndex, 1, &queuePriority);
	std::array<const char*, 4> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME };
	vk::DeviceCreateInfo deviceInfo({}, queueInfo, {}, deviceExtensions);
	vk::raii::Device device(physicalDevice, deviceInfo);
	vk::raii::Queue queue = device.getQueue(queueFamilyIndex, 0);

	// create command pool and command buffer
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::CommandPoolCreateInfo cmdPoolInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueFamilyIndex);
	vk::raii::CommandPool cmdPool(device, cmdPoolInfo);
	vk::CommandBufferAllocateInfo allocateInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
	vk::raii::CommandBuffers cmdBufs(device, allocateInfo);
	vk::raii::CommandBuffer cmdBuf(std::move(cmdBufs[0]));

	// create swapchain and image views
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::SurfaceCapabilitiesKHR surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
	vk::Format swapchainFormat = vk::Format::eB8G8R8A8Srgb;
	vk::Extent2D swapchainExtent = surfaceCapabilities.currentExtent;
	vk::SwapchainCreateInfoKHR swapchainInfo({}, *surface, surfaceCapabilities.minImageCount + 1, swapchainFormat, vk::ColorSpaceKHR::eSrgbNonlinear, swapchainExtent, 1, vk::ImageUsageFlagBits::eColorAttachment);
	swapchainInfo.clipped = VK_TRUE;
	swapchainInfo.presentMode = vk::PresentModeKHR::eMailbox;
	vk::raii::SwapchainKHR swapchain(device, swapchainInfo);

	std::vector<vk::Image> swapchainImages = swapchain.getImages();
	std::vector<vk::raii::ImageView> swapchainViews;
	swapchainViews.reserve(swapchainImages.size());
	for (vk::Image image : swapchainImages) {
		vk::ImageViewCreateInfo viewInfo({}, image, vk::ImageViewType::e2D, swapchainFormat);
		viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.layerCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;

		swapchainViews.push_back(vk::raii::ImageView(device, viewInfo));
	}

	// create depth buffer
	/////////////////////////////////////////////////////////////////////////////////////////////
	Allocator vmaAllocator(*instance, *physicalDevice, *device);

	AllocatedImage depthImage(vmaAllocator, VK_FORMAT_D32_SFLOAT, { swapchainExtent.width, swapchainExtent.height, 1 }, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

	vk::ImageViewCreateInfo viewInfo({}, vk::Image(depthImage), vk::ImageViewType::e2D, vk::Format(depthImage.getFormat()));
	viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.layerCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	vk::raii::ImageView depthView(device, viewInfo);

	// create gbuffer renderpass
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::AttachmentDescription colorAttachment({}, swapchainFormat);
	colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
	colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;
	vk::AttachmentReference colorAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal);

	vk::AttachmentDescription depthAttachment({}, vk::Format(depthImage.getFormat()));
	depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
	depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eClear;
	depthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
	vk::AttachmentReference depthAttachmentRef(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);

	vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, colorAttachmentRef, {}, &depthAttachmentRef);
	vk::SubpassDependency dependency(VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, vk::AccessFlagBits::eColorAttachmentWrite);

	std::array<vk::AttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };
	vk::RenderPassCreateInfo renderPassInfo({}, attachments, subpass, dependency);
	vk::raii::RenderPass gbufferPass(device, renderPassInfo);

	// create gbuffer shader modules
	/////////////////////////////////////////////////////////////////////////////////////////////
	std::vector<uint32_t> gbufferVertCode = readShader("shaders/gbuffer.vert.spv");
	std::vector<uint32_t> gbufferFragCode = readShader("shaders/gbuffer.frag.spv");

	vk::ShaderModuleCreateInfo gbufferVertInfo({}, gbufferVertCode);
	vk::ShaderModuleCreateInfo gbufferFragInfo({}, gbufferFragCode);
	vk::raii::ShaderModule gbufferVertModule(device, gbufferVertInfo);
	vk::raii::ShaderModule gbufferFragModule(device, gbufferFragInfo);

	// create camera uniforms buffer
	/////////////////////////////////////////////////////////////////////////////////////////////
	CameraUniforms camUniforms;
	camUniforms.model = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	camUniforms.view = glm::lookAt(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f));
	camUniforms.proj = glm::perspective(glm::radians(45.0f), swapchainExtent.width / (float)swapchainExtent.height, 0.1f, 10.0f);
	camUniforms.proj[1][1] *= -1;

	AllocatedBuffer uniformBuffer(vmaAllocator, sizeof(CameraUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	void* uniformData = uniformBuffer.map();
	memcpy(uniformData, &camUniforms, sizeof(CameraUniforms));
	uniformBuffer.unmap();

	// create descriptor pool and descriptor set
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::DescriptorPoolSize descriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1);
	vk::DescriptorPoolCreateInfo descriptorPoolInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, descriptorPoolSize);
	vk::raii::DescriptorPool descriptorPool(device, descriptorPoolInfo);

	vk::DescriptorSetLayoutBinding descriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex);
	vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutInfo({}, descriptorSetLayoutBinding);
	vk::raii::DescriptorSetLayout descriptorSetLayout(device, descriptorSetLayoutInfo);
	vk::DescriptorSetAllocateInfo descriptorSetAllocateInfo(*descriptorPool, *descriptorSetLayout);
	vk::raii::DescriptorSets descriptorSets(device, descriptorSetAllocateInfo);
	vk::raii::DescriptorSet descriptorSet(std::move(descriptorSets[0]));

	// update descriptor set
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::DescriptorBufferInfo descriptorBufferInfo(vk::Buffer(uniformBuffer), 0, sizeof(CameraUniforms));
	vk::WriteDescriptorSet writeDescriptorSet(*descriptorSet, 0, 0, vk::DescriptorType::eUniformBuffer, {}, descriptorBufferInfo);
	device.updateDescriptorSets(writeDescriptorSet, {});

	// create gbuffer pipeline layout
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::PipelineLayoutCreateInfo layoutInfo({}, *descriptorSetLayout);
	vk::raii::PipelineLayout gbufferLayout(device, layoutInfo);

	// create vertex and index buffer
	/////////////////////////////////////////////////////////////////////////////////////////////
	/*
	const std::vector<Vertex> vertices = {
		{{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
		{{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
		{{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
		{{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}}
	};
	const std::vector<uint16_t> indices = { 0, 1, 2, 2, 3, 0 };
	*/

	tinyobj::ObjReader objReader;
	objReader.ParseFromFile("dragon.obj");
	tinyobj::shape_t shape = objReader.GetShapes()[0];
	tinyobj::attrib_t attrib = objReader.GetAttrib();

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	size_t indexOffset = 0;
	for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
		int fv = 3;
		for (size_t v = 0; v < fv; v++) {
			tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];

			tinyobj::real_t vx = attrib.vertices[3 * idx.vertex_index + 0];
			tinyobj::real_t vy = attrib.vertices[3 * idx.vertex_index + 1];
			tinyobj::real_t vz = attrib.vertices[3 * idx.vertex_index + 2];

			tinyobj::real_t nx = attrib.normals[3 * idx.normal_index + 0];
			tinyobj::real_t ny = attrib.normals[3 * idx.normal_index + 1];
			tinyobj::real_t nz = attrib.normals[3 * idx.normal_index + 2];

			Vertex vert;
			vert.position.x = vx;
			vert.position.y = vy;
			vert.position.z = vz;

			vert.normal.x = nx;
			vert.normal.y = ny;
			vert.normal.z = nz;

			vertices.push_back(vert);
			indices.push_back(indexOffset + v);
		}
		indexOffset += fv;
	}

	AllocatedBuffer vertexBuffer(vmaAllocator, vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	void* vertexData = vertexBuffer.map();
	memcpy(vertexData, vertices.data(), vertices.size() * sizeof(Vertex));
	vertexBuffer.unmap();

	AllocatedBuffer indexBuffer(vmaAllocator, indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	void* indexData = indexBuffer.map();
	memcpy(indexData, indices.data(), indices.size() * sizeof(uint32_t));
	indexBuffer.unmap();

	// create gbuffer pipeline
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::PipelineShaderStageCreateInfo gbufferVertStage({}, vk::ShaderStageFlagBits::eVertex, *gbufferVertModule, "main");
	vk::PipelineShaderStageCreateInfo gbufferFragStage({}, vk::ShaderStageFlagBits::eFragment, *gbufferFragModule, "main");
	std::array<vk::PipelineShaderStageCreateInfo, 2> gbufferShaderStages = { gbufferVertStage, gbufferFragStage };

	vk::VertexInputBindingDescription vertexBinding(0, sizeof(Vertex));
	vk::VertexInputAttributeDescription positionAttribute(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position));
	vk::VertexInputAttributeDescription normalAttribute(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal));
	std::array<vk::VertexInputAttributeDescription, 2> vertexAttributes = { positionAttribute, normalAttribute };
	vk::PipelineVertexInputStateCreateInfo vertexInputInfo({}, vertexBinding, vertexAttributes);

	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo({}, vk::PrimitiveTopology::eTriangleList);

	vk::Viewport viewport(0.0f, 0.0f, swapchainExtent.width, swapchainExtent.height, 0.0f, 1.0f);
	vk::Rect2D scissor({ 0, 0 }, swapchainExtent);
	vk::PipelineViewportStateCreateInfo viewportInfo({}, viewport, scissor);

	vk::PipelineRasterizationStateCreateInfo rasterizerInfo;
	rasterizerInfo.frontFace = vk::FrontFace::eCounterClockwise;
	rasterizerInfo.cullMode = vk::CullModeFlagBits::eBack;
	rasterizerInfo.lineWidth = 1.0f;
	vk::PipelineMultisampleStateCreateInfo multisampleInfo;
	vk::PipelineColorBlendAttachmentState colorBlendAttachmentInfo;
	colorBlendAttachmentInfo.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
	vk::PipelineColorBlendStateCreateInfo colorBlendInfo({}, VK_FALSE, vk::LogicOp::eNoOp, colorBlendAttachmentInfo);

	vk::PipelineDepthStencilStateCreateInfo depthStencilInfo({}, VK_TRUE, VK_TRUE, vk::CompareOp::eLess, VK_FALSE, VK_FALSE);

	vk::GraphicsPipelineCreateInfo pipelineInfo({}, gbufferShaderStages, &vertexInputInfo, &inputAssemblyInfo, nullptr, &viewportInfo, &rasterizerInfo, &multisampleInfo, &depthStencilInfo, &colorBlendInfo, nullptr, *gbufferLayout, *gbufferPass);
	vk::raii::Pipeline gbufferPipeline(device, nullptr, pipelineInfo);

	// create gbuffer framebuffers
	/////////////////////////////////////////////////////////////////////////////////////////////
	std::vector<vk::raii::Framebuffer> gbufferFramebuffers;
	gbufferFramebuffers.reserve(swapchainViews.size());
	for (vk::raii::ImageView& imageView : swapchainViews) {
		std::array<vk::ImageView, 2> attachments = { *imageView, *depthView };
		vk::FramebufferCreateInfo frameBufferInfo({}, *gbufferPass, attachments, swapchainExtent.width, swapchainExtent.height, 1);
		gbufferFramebuffers.push_back(vk::raii::Framebuffer(device, frameBufferInfo));
	}

	// render loop
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::raii::Semaphore imageAcquiredSemaphore(device, vk::SemaphoreCreateInfo());
	vk::raii::Semaphore renderFinishedSemaphore(device, vk::SemaphoreCreateInfo());
	vk::raii::Fence inFlightFence(device, vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled));
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

		vk::Result result;
		result = device.waitForFences(*inFlightFence, VK_TRUE, UINT64_MAX);
		device.resetFences(*inFlightFence);

		uint32_t imageIndex;
		std::tie(result, imageIndex) = swapchain.acquireNextImage(UINT64_MAX, *imageAcquiredSemaphore);

		cmdBuf.begin({});
		std::array<vk::ClearValue, 2> clearCols = { vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f), vk::ClearDepthStencilValue(1.0f) };
		vk::RenderPassBeginInfo gbufferPassBeginInfo(*gbufferPass, *gbufferFramebuffers[imageIndex], scissor, clearCols);
		cmdBuf.beginRenderPass(gbufferPassBeginInfo, vk::SubpassContents::eInline);
		cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, *gbufferPipeline);
		cmdBuf.bindVertexBuffers(0, vk::Buffer(vertexBuffer), { 0 });
		cmdBuf.bindIndexBuffer(vk::Buffer(indexBuffer), { 0 }, vk::IndexType::eUint32);
		cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *gbufferLayout, 0, *descriptorSet, {});
		cmdBuf.drawIndexed(indices.size(), 1, 0, 0, 0);
		cmdBuf.endRenderPass();
		cmdBuf.end();

		vk::PipelineStageFlags waitDstStage(vk::PipelineStageFlagBits::eColorAttachmentOutput);
		vk::SubmitInfo submitInfo(*imageAcquiredSemaphore, waitDstStage, *cmdBuf, *renderFinishedSemaphore);
		queue.submit(submitInfo, *inFlightFence);

		vk::PresentInfoKHR presentInfo(*renderFinishedSemaphore, *swapchain, imageIndex);
		result = queue.presentKHR(presentInfo);
	}

	device.waitIdle();

	glfwDestroyWindow(window);
	glfwTerminate();
}