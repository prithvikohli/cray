#include "mesh.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <fstream>

static const int WINDOW_WIDTH = 2560;
static const int WINDOW_HEIGHT = 1440;

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
	vk::Format swapchainFormat = vk::Format::eB8G8R8A8Unorm;
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

	// create camera
	/////////////////////////////////////////////////////////////////////////////////////////////
	CameraUniforms cam;
	cam.view = glm::lookAt(glm::vec3(5.0f, -3.5f, -10.0f), glm::vec3(0.0f, -3.5f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
	cam.proj = glm::perspective(glm::radians(45.0f), swapchainExtent.width / (float)swapchainExtent.height, 0.1f, 100.0f);
	cam.proj[1][1] *= -1;

	// load GLTF scene
	/////////////////////////////////////////////////////////////////////////////////////////////
	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	std::string warn;
	std::string err;
	bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, "AntiqueCamera.glb");
	if (!warn.empty())
		std::cerr << "[WARN] " << warn;
	if (!err.empty())
		std::cerr << "[ERROR] " << err;
	if (!ret)
		throw std::runtime_error("failed to parse GLTF file!");

	// create descriptor pool and descriptor set layout
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::DescriptorPoolSize descriptorPoolSize(vk::DescriptorType::eUniformBuffer, model.meshes.size());
	vk::DescriptorPoolCreateInfo descriptorPoolInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, model.meshes.size(), descriptorPoolSize);
	vk::raii::DescriptorPool descriptorPool(device, descriptorPoolInfo);

	vk::DescriptorSetLayoutBinding descriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex);
	vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutInfo({}, descriptorSetLayoutBinding);
	vk::raii::DescriptorSetLayout descriptorSetLayout(device, descriptorSetLayoutInfo);

	// create meshes
	/////////////////////////////////////////////////////////////////////////////////////////////
	std::list<Mesh> meshes;
	for (tinygltf::Node& node : model.nodes) {
		tinygltf::Mesh m = model.meshes[node.mesh];
		meshes.emplace_back(device, descriptorPool, descriptorSetLayout, vmaAllocator, model, m);

		glm::mat4 s = glm::scale(glm::mat4(1.0f), glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
		glm::mat4 r = glm::toMat4(glm::quat(node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]));
		//glm::mat4 t = glm::translate(glm::mat4(1.0f), glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
		//cam.model = t * r * s;
		cam.model = r * s;
		meshes.back().update(cam);
	}
	
	// create gbuffer pipeline layout
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::PipelineLayoutCreateInfo layoutInfo({}, *descriptorSetLayout);
	vk::raii::PipelineLayout gbufferLayout(device, layoutInfo);

	// create gbuffer pipeline
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::PipelineShaderStageCreateInfo gbufferVertStage({}, vk::ShaderStageFlagBits::eVertex, *gbufferVertModule, "main");
	vk::PipelineShaderStageCreateInfo gbufferFragStage({}, vk::ShaderStageFlagBits::eFragment, *gbufferFragModule, "main");
	std::array<vk::PipelineShaderStageCreateInfo, 2> gbufferShaderStages = { gbufferVertStage, gbufferFragStage };

	vk::VertexInputBindingDescription vertexBinding(0, sizeof(Vertex));
	vk::VertexInputAttributeDescription positionAttribute(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, position));
	vk::VertexInputAttributeDescription normalAttribute(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal));
	vk::VertexInputAttributeDescription texAttribute(2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texCoord));
	std::array<vk::VertexInputAttributeDescription, 3> vertexAttributes = { positionAttribute, normalAttribute, texAttribute };
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
		for (Mesh& mesh : meshes)
			mesh.draw(gbufferLayout, cmdBuf);
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