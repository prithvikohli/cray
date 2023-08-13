#include <vulkan/vulkan_raii.hpp>

#define GLFW_INCLUDE_NONE
#include<GLFW/glfw3.h>

#include <iostream>
#include <fstream>

static const int WINDOW_WIDTH = 1920;
static const int WINDOW_HEIGHT = 1080;

std::vector<uint32_t> readFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	size_t fileSize = static_cast<size_t>(file.tellg());
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
	glfwCreateWindowSurface(static_cast<VkInstance>(*instance), window, nullptr, &surf);
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


	// create swapchain and image views
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::SurfaceCapabilitiesKHR surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
	vk::Format swapchainFormat = vk::Format::eB8G8R8A8Srgb;
	vk::Extent2D swapchainExtent = surfaceCapabilities.currentExtent;
	vk::SwapchainCreateInfoKHR swapchainInfo({}, *surface, surfaceCapabilities.minImageCount + 1, swapchainFormat, vk::ColorSpaceKHR::eSrgbNonlinear, surfaceCapabilities.currentExtent, 1, vk::ImageUsageFlagBits::eColorAttachment);
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

	// create gbuffer renderpass
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::AttachmentDescription colorAttachment({}, swapchainFormat);
	colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
	colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;
	vk::AttachmentReference colorAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal);

	vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, colorAttachmentRef);
	// check this
	vk::SubpassDependency dependency(VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, vk::AccessFlagBits::eColorAttachmentWrite);

	vk::RenderPassCreateInfo renderPassInfo({}, colorAttachment, subpass, dependency);
	vk::raii::RenderPass gbufferPass(device, renderPassInfo);

	// create gbuffer shader modules
	/////////////////////////////////////////////////////////////////////////////////////////////
	std::vector<uint32_t> gbufferVertCode = readFile("shaders/gbuffer.vert.spv");
	std::vector<uint32_t> gbufferFragCode = readFile("shaders/gbuffer.frag.spv");

	vk::ShaderModuleCreateInfo gbufferVertInfo({}, gbufferVertCode);
	vk::ShaderModuleCreateInfo gbufferFragInfo({}, gbufferFragCode);
	vk::raii::ShaderModule gbufferVertModule(device, gbufferVertInfo);
	vk::raii::ShaderModule gbufferFragModule(device, gbufferFragInfo);

	// create gbuffer pipeline layout
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::PipelineLayoutCreateInfo layoutInfo({}, {});
	vk::raii::PipelineLayout gbufferLayout(device, layoutInfo);

	// create gbuffer pipeline
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::PipelineShaderStageCreateInfo gbufferVertStage({}, vk::ShaderStageFlagBits::eVertex, *gbufferVertModule, "main");
	vk::PipelineShaderStageCreateInfo gbufferFragStage({}, vk::ShaderStageFlagBits::eFragment, *gbufferFragModule, "main");
	std::array<vk::PipelineShaderStageCreateInfo, 2> gbufferShaderStages = { gbufferVertStage, gbufferFragStage };

	vk::PipelineVertexInputStateCreateInfo vertexInputInfo;
	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo({}, vk::PrimitiveTopology::eTriangleList);

	vk::Viewport viewport(0.0f, 0.0f, static_cast<float>(swapchainExtent.width), static_cast<float>(swapchainExtent.height), 0.0f, 1.0f);
	vk::Rect2D scissor({ 0, 0 }, swapchainExtent);
	vk::PipelineViewportStateCreateInfo viewportInfo({}, viewport, scissor);

	vk::PipelineRasterizationStateCreateInfo rasterizerInfo;
	rasterizerInfo.frontFace = vk::FrontFace::eClockwise;
	rasterizerInfo.cullMode = vk::CullModeFlagBits::eBack;
	rasterizerInfo.lineWidth = 1.0f;
	vk::PipelineMultisampleStateCreateInfo multisampleInfo;
	vk::PipelineColorBlendAttachmentState colorBlendAttachmentInfo;
	colorBlendAttachmentInfo.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
	vk::PipelineColorBlendStateCreateInfo colorBlendInfo({}, VK_FALSE, vk::LogicOp::eNoOp, colorBlendAttachmentInfo);

	vk::GraphicsPipelineCreateInfo pipelineInfo({}, gbufferShaderStages, &vertexInputInfo, &inputAssemblyInfo, nullptr, &viewportInfo, &rasterizerInfo, &multisampleInfo, {}, &colorBlendInfo, nullptr, *gbufferLayout, *gbufferPass);
	vk::raii::Pipeline gbufferPipeline(device, nullptr, pipelineInfo);

	// create gbuffer framebuffers
	/////////////////////////////////////////////////////////////////////////////////////////////
	std::vector<vk::raii::Framebuffer> gbufferFramebuffers;
	gbufferFramebuffers.reserve(swapchainViews.size());
	for (vk::raii::ImageView& imageView : swapchainViews) {
		vk::FramebufferCreateInfo frameBufferInfo({}, *gbufferPass, *imageView, swapchainExtent.width, swapchainExtent.height, 1);
		gbufferFramebuffers.push_back(vk::raii::Framebuffer(device, frameBufferInfo));
	}

	// create command pool and command buffer
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::CommandPoolCreateInfo cmdPoolInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, queueFamilyIndex);
	vk::raii::CommandPool cmdPool(device, cmdPoolInfo);
	vk::CommandBufferAllocateInfo allocateInfo(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
	vk::raii::CommandBuffers cmdBufs(device, allocateInfo);
	vk::raii::CommandBuffer cmdBuf(std::move(cmdBufs[0]));

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
		vk::ClearValue clearCol(vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f));
		vk::RenderPassBeginInfo gbufferPassBeginInfo(*gbufferPass, *gbufferFramebuffers[imageIndex], scissor, clearCol);
		cmdBuf.beginRenderPass(gbufferPassBeginInfo, vk::SubpassContents::eInline);
		cmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, *gbufferPipeline);
		cmdBuf.draw(3, 1, 0, 0);
		cmdBuf.endRenderPass();
		cmdBuf.end();

		vk::PipelineStageFlags waitDstStage(vk::PipelineStageFlagBits::eColorAttachmentOutput);
		vk::SubmitInfo submitInfo(*imageAcquiredSemaphore, waitDstStage, *cmdBuf, *renderFinishedSemaphore);
		queue.submit(submitInfo, *inFlightFence);

		vk::PresentInfoKHR presentInfo(*renderFinishedSemaphore, *swapchain, imageIndex);
		result = queue.presentKHR(presentInfo);
	}

	glfwDestroyWindow(window);
	glfwTerminate();
}