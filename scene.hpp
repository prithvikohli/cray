#pragma once

#include <vulkan/vulkan_raii.hpp>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "allocations.hpp"

#include <iostream>

struct CameraUniforms {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

struct GltfBuffer {
    std::unique_ptr<AllocatedBuffer> allocatedBuffer;
    size_t byteLength;

    GltfBuffer(VmaAllocator allocator, tinygltf::Buffer& buf) {
        byteLength = buf.data.size();
        // TODO different buffer usages
        // TODO different memory usages
        allocatedBuffer = std::make_unique<AllocatedBuffer>(allocator, byteLength, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        void* data = allocatedBuffer->map();
        memcpy(data, buf.data.data(), byteLength);
        allocatedBuffer->unmap();
    }
};

struct GltfBufferView {
    std::shared_ptr<GltfBuffer> buffer;
    size_t byteLength;
    size_t byteOffset;
    // TODO other properties

    GltfBufferView(std::shared_ptr<GltfBuffer> buf, tinygltf::BufferView& bufView) : buffer(buf), byteLength(bufView.byteLength), byteOffset(bufView.byteOffset) {};
};

struct GltfAccessor {
    std::shared_ptr<GltfBufferView> bufferView;
    size_t byteOffset;
    size_t count;
    // TODO other properties

    GltfAccessor(std::shared_ptr<GltfBufferView> bufView, tinygltf::Accessor& accessor) : bufferView(bufView), byteOffset(accessor.byteOffset), count(accessor.count) {};
};

struct Mesh {
    std::vector<vk::Buffer> vertexBuffers;
    std::vector<vk::DeviceSize> vertexBufferOffsets;

    vk::Buffer indexBuffer;
    vk::DeviceSize indexBufferOffset;
    
    std::unique_ptr<AllocatedBuffer> uniformBuffer;

    std::shared_ptr<vk::raii::DescriptorSet> descriptorSet;

    uint32_t indexCount;

    Mesh(VmaAllocator allocator, GltfAccessor& positionAccessor, GltfAccessor& normalAccessor, GltfAccessor& texCoordAccessor, GltfAccessor& indexAccessor, std::shared_ptr<vk::raii::DescriptorSet> descriptorSet) : descriptorSet(descriptorSet) {
        // TODO other properties
        vk::Buffer positionBuffer(*positionAccessor.bufferView->buffer->allocatedBuffer);
        vk::Buffer normalBuffer(*normalAccessor.bufferView->buffer->allocatedBuffer);
        vk::Buffer texCoordBuffer(*texCoordAccessor.bufferView->buffer->allocatedBuffer);

        vertexBuffers.push_back(positionBuffer);
        vertexBuffers.push_back(normalBuffer);
        vertexBuffers.push_back(texCoordBuffer);

        vk::DeviceSize positionOffset = static_cast<vk::DeviceSize>(positionAccessor.byteOffset + positionAccessor.bufferView->byteOffset);
        vk::DeviceSize normalOffset = static_cast<vk::DeviceSize>(normalAccessor.byteOffset + normalAccessor.bufferView->byteOffset);
        vk::DeviceSize texCoordOffset = static_cast<vk::DeviceSize>(texCoordAccessor.byteOffset + texCoordAccessor.bufferView->byteOffset);

        vertexBufferOffsets.push_back(positionOffset);
        vertexBufferOffsets.push_back(normalOffset);
        vertexBufferOffsets.push_back(texCoordOffset);

        indexBuffer = *indexAccessor.bufferView->buffer->allocatedBuffer;
        indexBufferOffset = static_cast<vk::DeviceSize>(indexAccessor.byteOffset + indexAccessor.bufferView->byteOffset);

        uniformBuffer = std::make_unique<AllocatedBuffer>(allocator, sizeof(CameraUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

        indexCount = indexAccessor.count;
    }

    void update(CameraUniforms camUniforms) {
        void* data = uniformBuffer->map();
        memcpy(data, &camUniforms, sizeof(CameraUniforms));
        uniformBuffer->unmap();
    }
};

//class Mesh {
//public:
//	Mesh(vk::raii::Device&, vk::raii::DescriptorPool&, vk::raii::DescriptorSetLayout& descriptorSetLayout, VmaAllocator allocator, tinygltf::Model& model, tinygltf::Mesh& mesh);
//
//	void update(CameraUniforms camUniforms);
//	void draw(vk::raii::PipelineLayout& layout, vk::raii::CommandBuffer& cmdBuf);
//private:
//	std::unique_ptr<AllocatedBuffer> m_vertexBuffer;
//	std::unique_ptr<AllocatedBuffer> m_indexBuffer;
//	std::unique_ptr<AllocatedBuffer> m_uniformBuffer;
//
//	std::unique_ptr<vk::raii::DescriptorSet> m_descriptorSet;
//
//	uint32_t m_indexCount;
//};
//
//Mesh::Mesh(vk::raii::Device& device, vk::raii::DescriptorPool& descriptorPool, vk::raii::DescriptorSetLayout& descriptorSetLayout, VmaAllocator allocator, tinygltf::Model& model, tinygltf::Mesh& mesh) {
//	tinygltf::Primitive primitive = mesh.primitives[0];
//	tinygltf::Accessor posAccessor = model.accessors[primitive.attributes.at("POSITION")];
//	tinygltf::Accessor normAccessor = model.accessors[primitive.attributes.at("NORMAL")];
//	tinygltf::Accessor texAccessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
//
//	tinygltf::BufferView posBufferView = model.bufferViews[posAccessor.bufferView];
//	tinygltf::BufferView normBufferView = model.bufferViews[normAccessor.bufferView];
//	tinygltf::BufferView texBufferView = model.bufferViews[texAccessor.bufferView];
//
//	tinygltf::Buffer posBuffer = model.buffers[posBufferView.buffer];
//	tinygltf::Buffer normBuffer = model.buffers[normBufferView.buffer];
//	tinygltf::Buffer texBuffer = model.buffers[texBufferView.buffer];
//
//	std::vector<Vertex> vertices;
//	vertices.reserve(posAccessor.count);
//	for (int i = 0; i < posAccessor.count; i++) {
//		float* pos = reinterpret_cast<float*>(posBuffer.data.data() + posBufferView.byteOffset);
//		float* norm = reinterpret_cast<float*>(normBuffer.data.data() + normBufferView.byteOffset);
//		float* tex = reinterpret_cast<float*>(texBuffer.data.data() + texBufferView.byteOffset);
//
//		Vertex v;
//		v.position = { pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2] };
//		v.normal = { norm[i * 3], norm[i * 3 + 1], norm[i * 3 + 2] };
//		v.texCoord = { tex[i * 2], tex[i * 2 + 1] };
//		vertices.push_back(v);
//	}
//
//	m_vertexBuffer = std::make_unique<AllocatedBuffer>(allocator, vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
//	void* vertexData = m_vertexBuffer->map();
//	memcpy(vertexData, vertices.data(), vertices.size() * sizeof(Vertex));
//	m_vertexBuffer->unmap();
//
//	tinygltf::Accessor indicesAccessor = model.accessors[primitive.indices];
//	tinygltf::BufferView indicesBufferView = model.bufferViews[indicesAccessor.bufferView];
//	tinygltf::Buffer indicesBuffer = model.buffers[indicesBufferView.buffer];
//
//	m_indexBuffer = std::make_unique<AllocatedBuffer>(allocator, indicesBufferView.byteLength, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
//	void* indexData = m_indexBuffer->map();
//	memcpy(indexData, indicesBuffer.data.data() + indicesBufferView.byteOffset, indicesBufferView.byteLength);
//	m_indexBuffer->unmap();
//
//	m_indexCount = indicesAccessor.count;
//
//	m_uniformBuffer = std::make_unique<AllocatedBuffer>(allocator, sizeof(CameraUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
//
//	vk::DescriptorSetAllocateInfo descriptorSetAllocateInfo(*descriptorPool, *descriptorSetLayout);
//	vk::raii::DescriptorSets descriptorSets(device, descriptorSetAllocateInfo);
//	m_descriptorSet = std::make_unique<vk::raii::DescriptorSet>(std::move(descriptorSets[0]));
//
//	vk::DescriptorBufferInfo descriptorBufferInfo(vk::Buffer(*m_uniformBuffer), 0, sizeof(CameraUniforms));
//	vk::WriteDescriptorSet writeDescriptorSet(**m_descriptorSet, 0, 0, vk::DescriptorType::eUniformBuffer, {}, descriptorBufferInfo);
//	device.updateDescriptorSets(writeDescriptorSet, {});
//}
//
//void Mesh::update(CameraUniforms camUniforms) {
//	void* uniformData = m_uniformBuffer->map();
//	memcpy(uniformData, &camUniforms, sizeof(camUniforms));
//	m_uniformBuffer->unmap();
//}
//
//void Mesh::draw(vk::raii::PipelineLayout& layout, vk::raii::CommandBuffer& cmdBuf) {
//	cmdBuf.bindVertexBuffers(0, vk::Buffer(*m_vertexBuffer), { 0 });
//	cmdBuf.bindIndexBuffer(vk::Buffer(*m_indexBuffer), { 0 }, vk::IndexType::eUint16);
//	cmdBuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *layout, 0, **m_descriptorSet, {});
//	cmdBuf.drawIndexed(m_indexCount, 1, 0, 0, 0);
//}
