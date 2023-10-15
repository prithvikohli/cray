#include "scene.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <fstream>

// TODO make configurable
static const int WINDOW_WIDTH = 2560;
static const int WINDOW_HEIGHT = 1440;

#if NDEBUG
static const std::array<const char*, 1> ENABLED_LAYERS = { };
#else
static const std::array<const char*, 1> ENABLED_LAYERS = { "VK_LAYER_KHRONOS_validation" };
#endif

static const std::array<const char*, 4> ENABLED_DEVICE_EXTENSIONS = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_QUERY_EXTENSION_NAME };

static vk::raii::Instance createVulkanInstance(const vk::raii::Context& context)
{
    const uint32_t apiVersion = context.enumerateInstanceVersion();
    vk::ApplicationInfo appInfo("cray", 1, nullptr, 0, apiVersion);

    uint32_t glfwExtensionCount;
    // TODO do we need an error check here?
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> instanceExtensions;
    instanceExtensions.reserve(glfwExtensionCount);
    for (uint32_t i = 0; i < glfwExtensionCount; i++)
        instanceExtensions.push_back(glfwExtensions[i]);

    vk::InstanceCreateInfo instanceInfo({}, &appInfo, ENABLED_LAYERS, instanceExtensions);
    return vk::raii::Instance(context, instanceInfo);
}

static vk::raii::SurfaceKHR createWindowSurface(const vk::raii::Instance& instance, GLFWwindow* window)
{
    VkSurfaceKHR surf;
    VkResult res = glfwCreateWindowSurface(*instance, window, nullptr, &surf);
    if (res != VK_SUCCESS)
        throw std::runtime_error("failed to create window surface!");
    return vk::raii::SurfaceKHR(instance, surf);
}

static vk::raii::PhysicalDevice createPhysicalDevice(const vk::raii::Instance& instance)
{
    vk::raii::PhysicalDevices physicalDevices(instance);
    int physicalDeviceIndex = -1;
    for (size_t i = 0; i < physicalDevices.size(); i++)
    {
        // TODO make sure we select the right discrete GPU
        if (physicalDevices[i].getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
        {
            physicalDeviceIndex = i;
            break;
        }
    }
    if (physicalDeviceIndex == -1)
        throw std::runtime_error("could not find appropriate GPU!");

    return physicalDevices[physicalDeviceIndex];
}

static uint32_t chooseGctQueue(const vk::raii::PhysicalDevice& physicalDevice)
{
    uint32_t queueFamilyIndex = 0u;
    std::vector<vk::QueueFamilyProperties> qfps = physicalDevice.getQueueFamilyProperties();
    for (size_t i = 0; i < qfps.size(); i++)
    {
        vk::QueueFlags& flags = qfps[i].queueFlags;
        if ((flags & vk::QueueFlagBits::eCompute) && (flags & vk::QueueFlagBits::eGraphics))
        {
            queueFamilyIndex = i;
            break;
        }
    }
    return queueFamilyIndex;
}

static vk::raii::Device createDevice(const vk::raii::PhysicalDevice& physicalDevice, uint32_t queueFamilyIndex)
{
    const float queuePriority = 0.0f;
    vk::DeviceQueueCreateInfo queueInfo({}, queueFamilyIndex, 1, &queuePriority);
    vk::DeviceCreateInfo deviceInfo({}, queueInfo, {}, ENABLED_DEVICE_EXTENSIONS);
    return vk::raii::Device(physicalDevice, deviceInfo);
}

static vk::raii::SwapchainKHR createSwapchain(const vk::SurfaceCapabilitiesKHR& surfaceCapabilities, const vk::raii::SurfaceKHR& surface, const vk::raii::Device& device)
{
    vk::SwapchainCreateInfoKHR swapchainInfo({}, *surface, surfaceCapabilities.minImageCount + 1, vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear, surfaceCapabilities.currentExtent, 1, vk::ImageUsageFlagBits::eColorAttachment);
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.presentMode = vk::PresentModeKHR::eFifo;
    return vk::raii::SwapchainKHR(device, swapchainInfo);
}

static std::vector<vk::raii::ImageView> createSwapchainViews(const vk::raii::SwapchainKHR& swapchain, const vk::raii::Device& device)
{
    const std::vector<vk::Image> swapchainImages = swapchain.getImages();
    std::vector<vk::raii::ImageView> swapchainViews;
    swapchainViews.reserve(swapchainImages.size());
    for (const vk::Image& image : swapchainImages) {
        vk::ImageViewCreateInfo viewInfo({}, image, vk::ImageViewType::e2D, vk::Format::eB8G8R8A8Srgb);
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.layerCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;

        swapchainViews.push_back(vk::raii::ImageView(device, viewInfo));
    }

    return swapchainViews;
}

static std::vector<uint32_t> readShader(const std::string& filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        throw std::runtime_error("failed to open file \"" + filename + "\"!");
    size_t fileSize = file.tellg();
    std::vector<uint32_t> buffer((fileSize + 3u) / 4u);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();
    return buffer;
}

int main()
{
    // initialise GLFW and create window
    /////////////////////////////////////////////////////////////////////////////////////////////
    int res = glfwInit();
    if (res == GLFW_FALSE)
        throw std::runtime_error("failed to initialize GLFW!");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "cray", nullptr, nullptr);
    if (!window)
        throw std::runtime_error("failed to create GLFW window!");

    // create Vulkan instance
    /////////////////////////////////////////////////////////////////////////////////////////////
    vk::raii::Context context;
    vk::raii::Instance instance = createVulkanInstance(context);

    // create surface
    /////////////////////////////////////////////////////////////////////////////////////////////
    vk::raii::SurfaceKHR surface = createWindowSurface(instance, window);

    // create physical device
    /////////////////////////////////////////////////////////////////////////////////////////////
    vk::raii::PhysicalDevice physicalDevice = createPhysicalDevice(instance);

    // choose GCT queue
    /////////////////////////////////////////////////////////////////////////////////////////////
    uint32_t queueFamilyIndex = chooseGctQueue(physicalDevice);

    // create device and queue
    /////////////////////////////////////////////////////////////////////////////////////////////
    vk::raii::Device device = createDevice(physicalDevice, queueFamilyIndex);
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
    vk::SurfaceCapabilitiesKHR surfaceCapabilites = physicalDevice.getSurfaceCapabilitiesKHR(*surface);
    vk::raii::SwapchainKHR swapchain = createSwapchain(surfaceCapabilites, surface, device);

    std::vector<vk::raii::ImageView> swapchainViews = createSwapchainViews(swapchain, device);

    vk::Extent2D swapchainExtent = surfaceCapabilites.currentExtent;
    vk::Format swapchainFormat = vk::Format::eB8G8R8A8Srgb;

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
        std::cerr << "[WRN] " << warn << std::endl;
    if (!err.empty())
        std::cerr << "[ERR] " << err << std::endl;
    if (!ret)
        throw std::runtime_error("failed to parse GLTF file!");

    int geometryNodeCount = 0;
    for (tinygltf::Node& n : model.nodes) {
        if (n.mesh > -1)
            geometryNodeCount++;
    }

    // create descriptor pool and descriptor set layout
    /////////////////////////////////////////////////////////////////////////////////////////////
    vk::DescriptorPoolSize descriptorPoolSize(vk::DescriptorType::eUniformBuffer, geometryNodeCount);
    vk::DescriptorPoolCreateInfo descriptorPoolInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, geometryNodeCount, descriptorPoolSize);
    vk::raii::DescriptorPool descriptorPool(device, descriptorPoolInfo);

    vk::DescriptorSetLayoutBinding descriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex);
    vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutInfo({}, descriptorSetLayoutBinding);
    vk::raii::DescriptorSetLayout descriptorSetLayout(device, descriptorSetLayoutInfo);

    // create descriptor sets
    /////////////////////////////////////////////////////////////////////////////////////////////
    std::vector<std::shared_ptr<vk::raii::DescriptorSet>> descriptorSetPtrs;
    descriptorSetPtrs.reserve(geometryNodeCount);
    for (tinygltf::Node& node : model.nodes) {
        vk::DescriptorSetAllocateInfo descriptorSetAllocateInfo(*descriptorPool, *descriptorSetLayout);
        vk::raii::DescriptorSets descriptorSets(device, descriptorSetAllocateInfo);
        descriptorSetPtrs.push_back(std::make_shared<vk::raii::DescriptorSet>(std::move(descriptorSets[0])));
    }

    // create meshes
    /////////////////////////////////////////////////////////////////////////////////////////////
    std::vector<std::shared_ptr<GltfBuffer>> gltfBuffers;
    for (tinygltf::Buffer& buf : model.buffers)
        gltfBuffers.push_back(std::make_shared<GltfBuffer>(vmaAllocator, buf));
    std::vector<std::shared_ptr<GltfBufferView>> gltfBufferViews;
    for (int i = 0; i < model.bufferViews.size(); i++)
        gltfBufferViews.push_back(std::make_shared<GltfBufferView>(gltfBuffers[model.bufferViews[i].buffer], model.bufferViews[i]));
    std::vector<std::shared_ptr<GltfAccessor>> gltfAccessors;
    for (int i = 0; i < model.accessors.size(); i++)
        gltfAccessors.push_back(std::make_shared<GltfAccessor>(gltfBufferViews[model.accessors[i].bufferView], model.accessors[i]));

    std::list<Mesh> meshes;
    for (int i = 0; i < model.nodes.size(); i++) {
        tinygltf::Node node = model.nodes[i];
        // only geometry nodes
        if (node.mesh == -1)
            continue;
        // only first primitive
        tinygltf::Primitive primitive = model.meshes[node.mesh].primitives[0];
        // only triangles
        if (primitive.mode != 4)
            continue;

        // TODO other mesh properties
        GltfAccessor& positionAccessor = *gltfAccessors[primitive.attributes.at("POSITION")];
        GltfAccessor& normalAccessor = *gltfAccessors[primitive.attributes.at("NORMAL")];
        GltfAccessor& texCoordAccessor = *gltfAccessors[primitive.attributes.at("TEXCOORD_0")];
        GltfAccessor& indexAccessor = *gltfAccessors[primitive.indices];

        meshes.emplace_back(vmaAllocator, positionAccessor, normalAccessor, texCoordAccessor, indexAccessor, descriptorSetPtrs[i]);
        Mesh& m = meshes.back();

        // TODO recursive transform
        glm::mat4 t(1.0f), r(1.0f), s(1.0f);
        if (!node.scale.empty())
            s = glm::scale(glm::mat4(1.0f), glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
        if (!node.rotation.empty())
            r = glm::toMat4(glm::quat(node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]));
        if (!node.translation.empty())
            t = glm::translate(glm::mat4(1.0f), glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
        cam.model = t * r * s;

        m.update(cam);

        // update descriptor set
        vk::DescriptorBufferInfo descriptorBufferInfo(vk::Buffer(*m.uniformBuffer), 0, sizeof(CameraUniforms));
        vk::WriteDescriptorSet writeDescriptorSet(**m.descriptorSet, 0, 0, vk::DescriptorType::eUniformBuffer, {}, descriptorBufferInfo);
        device.updateDescriptorSets(writeDescriptorSet, {});
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

    // TODO different types from GLTF
    vk::VertexInputBindingDescription vertexBindingPosition(0, 3 * sizeof(float));
    vk::VertexInputBindingDescription vertexBindingNormal(1, 3 * sizeof(float));
    vk::VertexInputBindingDescription vertexBindingTexCoord(2, 2 * sizeof(float));
    std::array<vk::VertexInputBindingDescription, 3> vertexBindings = { vertexBindingPosition, vertexBindingNormal, vertexBindingTexCoord };
    vk::VertexInputAttributeDescription positionAttribute(0, 0, vk::Format::eR32G32B32Sfloat, 0);
    vk::VertexInputAttributeDescription normalAttribute(1, 1, vk::Format::eR32G32B32Sfloat, 0);
    vk::VertexInputAttributeDescription texAttribute(2, 2, vk::Format::eR32G32Sfloat, 0);
    std::array<vk::VertexInputAttributeDescription, 3> vertexAttributes = { positionAttribute, normalAttribute, texAttribute };
    vk::PipelineVertexInputStateCreateInfo vertexInputInfo({}, vertexBindings, vertexAttributes);

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
        for (Mesh& mesh : meshes) {
            cmdBuf.bindVertexBuffers(0, mesh.vertexBuffers, mesh.vertexBufferOffsets);
            // TODO different types from GLTF
            cmdBuf.bindIndexBuffer(mesh.indexBuffer, mesh.indexBufferOffset, vk::IndexType::eUint16);
            cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *gbufferLayout, 0, **mesh.descriptorSet, {});
            cmdBuf.drawIndexed(mesh.indexCount, 1, 0, 0, 0);
        }
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