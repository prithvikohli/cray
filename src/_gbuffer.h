#pragma once

#include "vk_graphics.h"

class GBufferPass
{
public:
    GBufferPass(vk::RenderContext* rc, const vk::Image* depthAttachment, const std::vector<const vk::Image*>& colorAttachments, const std::vector<uint32_t>& vertCode, const std::vector<uint32_t>& fragCode);
    GBufferPass(const GBufferPass&) = delete;

    ~GBufferPass();

    void begin(VkCommandBuffer cmdBuf, VkFramebuffer framebuf) const;
    void end(VkCommandBuffer cmdBuf) const;

    VkRenderPass getRenderPass() const { return m_renderPass; }

    GBufferPass& operator=(const GBufferPass&) = delete;
    operator VkRenderPass() const { return m_renderPass; }

    std::unique_ptr<vk::PipelineLayout> m_pipelineLayout;

private:
    VkDevice m_device;
    VkRenderPass m_renderPass;
    VkPipeline m_pipeline;

    VkExtent2D m_extent;

    uint32_t m_colorAttachmentCount;

    void createRenderPass(const vk::Image* depthImage, const std::vector<const vk::Image*>& colorImages);
    void createPipelineLayout(const std::vector<uint32_t>& vertCode, const std::vector<uint32_t>& fragCode);
    void createPipeline(const std::vector<uint32_t>& vertCode, const std::vector<uint32_t>& fragCode);
};