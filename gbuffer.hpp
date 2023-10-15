#pragma once

#include <vulkan/vulkan_raii.hpp>

#include "allocations.hpp"

struct GBufferBundle
{
    std::shared_ptr<vk::raii::ImageView> depthView;
    std::shared_ptr<vk::raii::ImageView> albedoMetallicView;
    std::shared_ptr<vk::raii::ImageView> normalRoughnessView;
    std::shared_ptr<vk::raii::ImageView> emissiveView;
};

class GBufferPass
{
public:
    GBufferPass(const vk::raii::Device& device, GBufferBundle gbuffer, const std::vector<uint32_t>& vertCode, const std::vector<uint32_t>& fragCode)
    {
        createRenderPass(device);
        createShaderModules(device, vertCode, fragCode);
    }
private:
    std::unique_ptr<vk::raii::RenderPass> m_renderPass;
    std::unique_ptr<vk::raii::ShaderModule> m_vertModule;
    std::unique_ptr<vk::raii::ShaderModule> m_fragModule;

    void createRenderPass(const vk::raii::Device& device)
    {
        vk::AttachmentDescription depthAttachment({}, vk::Format::eD32Sfloat);
        depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        depthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eClear;
        depthAttachment.finalLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

        vk::AttachmentDescription albedoMetallicAttachment({}, vk::Format::eR32G32B32A32Sfloat);
        albedoMetallicAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        albedoMetallicAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentDescription normalRoughnessAttachment({}, vk::Format::eR32G32B32A32Sfloat);
        normalRoughnessAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        normalRoughnessAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentDescription emissiveAttachment({}, vk::Format::eR32G32B32A32Sfloat);
        emissiveAttachment.loadOp = vk::AttachmentLoadOp::eClear;
        emissiveAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::AttachmentReference depthAttachmentRef(0, vk::ImageLayout::eDepthStencilAttachmentOptimal);
        vk::AttachmentReference albedoMetallicAttachmentRef(1, vk::ImageLayout::eColorAttachmentOptimal);
        vk::AttachmentReference normalRoughnessAttachmentRef(2, vk::ImageLayout::eColorAttachmentOptimal);
        vk::AttachmentReference emissiveAttachmentRef(3, vk::ImageLayout::eColorAttachmentOptimal);

        std::array<vk::AttachmentReference, 4> colorAttachmentRefs = { depthAttachmentRef, albedoMetallicAttachmentRef, normalRoughnessAttachmentRef, emissiveAttachmentRef };

        vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, colorAttachmentRefs, {}, &depthAttachmentRef);
        vk::SubpassDependency dependency(0, VK_SUBPASS_EXTERNAL, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eComputeShader, vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eShaderRead);

        std::array<vk::AttachmentDescription, 4> attachments = { depthAttachment, albedoMetallicAttachment, normalRoughnessAttachment, emissiveAttachment };
        vk::RenderPassCreateInfo renderPassInfo({}, attachments, subpass, dependency);
        m_renderPass = std::make_unique<vk::raii::RenderPass>(device, renderPassInfo);
    }

    void createShaderModules(const vk::raii::Device& device, const std::vector<uint32_t>& vertCode, const std::vector<uint32_t>& fragCode)
    {
        vk::ShaderModuleCreateInfo vertInfo({}, vertCode);
        vk::ShaderModuleCreateInfo fragInfo({}, fragCode);
        m_vertModule = std::make_unique<vk::raii::ShaderModule>(device, vertInfo);
        m_fragModule = std::make_unique<vk::raii::ShaderModule>(device, fragInfo);
    }
};