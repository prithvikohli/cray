#include <vulkan/vulkan_raii.hpp>

#define GLFW_INCLUDE_NONE
#include<GLFW/glfw3.h>

#include <iostream>
#include <fstream>

static const int WINDOW_WIDTH = 1280;
static const int WINDOW_HEIGHT = 720;

static std::vector<uint32_t> readFile(const std::string& filename) {
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
	uint32_t apiVersion = context.enumerateInstanceVersion();
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
	float queuePriority = 0.0f;
	vk::DeviceQueueCreateInfo queueInfo({}, queueFamilyIndex, 1, &queuePriority);
	std::array<const char*, 4> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME };
	vk::DeviceCreateInfo deviceInfo({}, queueInfo, {}, deviceExtensions);
	vk::raii::Device device(physicalDevice, deviceInfo);
	vk::raii::Queue queue = device.getQueue(queueFamilyIndex, 0);


	// create swapchain and image views
	/////////////////////////////////////////////////////////////////////////////////////////////
	vk::Format swapchainFormat = vk::Format::eB8G8R8A8Srgb;
	vk::SurfaceCapabilitiesKHR surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
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
	

	glfwDestroyWindow(window);
	glfwTerminate();
}