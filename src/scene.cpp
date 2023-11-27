#include "scene.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"

Scene::Scene(vk::RenderContext* rc, const std::string& gltfFilename, bool binary) : m_rc(rc)
{
    // parse file
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string warn;
    std::string err;
    bool ret;
    if (binary)
        ret = loader.LoadBinaryFromFile(&model, &err, &warn, gltfFilename);
    else
        ret = loader.LoadASCIIFromFile(&model, &err, &warn, gltfFilename);
    if (!warn.empty())
        LOG(warn);
    if (!err.empty())
        LOGE(err);
    if (!ret)
        throw std::runtime_error("failed to parse GLTF binary file!");

    for (tinygltf::Material& mat : model.materials)
        createMaterial(model, mat);

    for (tinygltf::Mesh& mesh : model.meshes)
        createMesh(model, mesh);

    // only load default scene, fallback on scene 0
    // TODO error check here
    tinygltf::Scene& scene = model.scenes[std::max(0, model.defaultScene)];
    for (int n : scene.nodes)
    {
        tinygltf::Node& node = model.nodes[n];
        createNode(model, node);
    }
}

Scene::~Scene()
{
    for (Node* n : m_nodes)
        delete n;
}

void Scene::createMaterial(tinygltf::Model& model, tinygltf::Material& material)
{
    // TODO error checking
    // TODO different GLTF image formats
    // TODO multiple tex coords
    // TODO texture factors
    tinygltf::Texture& albedoTex = model.textures[material.pbrMetallicRoughness.baseColorTexture.index];
    tinygltf::Texture& metallicRoughnessTex = model.textures[material.pbrMetallicRoughness.metallicRoughnessTexture.index];
    tinygltf::Texture& normalTex = model.textures[material.normalTexture.index];
    tinygltf::Texture& emissiveTex = model.textures[material.emissiveTexture.index];

    // TODO buffer views
    tinygltf::Image& albedoImg = model.images[albedoTex.source];
    tinygltf::Image& metallicRoughnessImg = model.images[metallicRoughnessTex.source];
    tinygltf::Image& normalImg = model.images[normalTex.source];
    tinygltf::Image& emissiveImg = model.images[emissiveTex.source];

    Material mat;
    {
        VkExtent3D extent = { static_cast<uint32_t>(albedoImg.width), static_cast<uint32_t>(albedoImg.height), 1u };
        std::shared_ptr<vk::Image> stagingImg = m_rc->createImage(VK_FORMAT_R8G8B8A8_UNORM, extent, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_TILING_LINEAR);

        void* data = stagingImg->map();
        memcpy(data, albedoImg.image.data(), albedoImg.image.size());
        stagingImg->unmap();

        mat.albedo = m_rc->createImage(VK_FORMAT_R8G8B8A8_UNORM, extent, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
        m_rc->copyStagingImage(*mat.albedo, *stagingImg, extent, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    {
        VkExtent3D extent = { static_cast<uint32_t>(metallicRoughnessImg.width), static_cast<uint32_t>(metallicRoughnessImg.height), 1u };
        std::shared_ptr<vk::Image> stagingImg = m_rc->createImage(VK_FORMAT_R8G8B8A8_UNORM, extent, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_TILING_LINEAR);

        void* data = stagingImg->map();
        memcpy(data, metallicRoughnessImg.image.data(), metallicRoughnessImg.image.size());
        stagingImg->unmap();

        mat.metallicRoughness = m_rc->createImage(VK_FORMAT_R8G8B8A8_UNORM, extent, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
        m_rc->copyStagingImage(*mat.metallicRoughness, *stagingImg, extent, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    {
        VkExtent3D extent = { static_cast<uint32_t>(normalImg.width), static_cast<uint32_t>(normalImg.height), 1u };
        std::shared_ptr<vk::Image> stagingImg = m_rc->createImage(VK_FORMAT_R8G8B8A8_UNORM, extent, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_TILING_LINEAR);

        void* data = stagingImg->map();
        memcpy(data, normalImg.image.data(), normalImg.image.size());
        stagingImg->unmap();

        mat.normal = m_rc->createImage(VK_FORMAT_R8G8B8A8_UNORM, extent, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
        m_rc->copyStagingImage(*mat.normal, *stagingImg, extent, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    {
        VkExtent3D extent = { static_cast<uint32_t>(emissiveImg.width), static_cast<uint32_t>(emissiveImg.height), 1u };
        std::shared_ptr<vk::Image> stagingImg = m_rc->createImage(VK_FORMAT_R8G8B8A8_UNORM, extent, VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_TILING_LINEAR);

        void* data = stagingImg->map();
        memcpy(data, emissiveImg.image.data(), emissiveImg.image.size());
        stagingImg->unmap();

        mat.emissive = m_rc->createImage(VK_FORMAT_R8G8B8A8_UNORM, extent, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
        m_rc->copyStagingImage(*mat.emissive, *stagingImg, extent, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    m_materials.push_back(mat);

    MaterialViews matViews;
    matViews.albedo = m_rc->createImageView(*mat.albedo, VK_IMAGE_ASPECT_COLOR_BIT);
    matViews.normal = m_rc->createImageView(*mat.normal, VK_IMAGE_ASPECT_COLOR_BIT);
    matViews.metallicRoughness = m_rc->createImageView(*mat.metallicRoughness, VK_IMAGE_ASPECT_COLOR_BIT);
    matViews.emissive = m_rc->createImageView(*mat.emissive, VK_IMAGE_ASPECT_COLOR_BIT);

    m_materialViews.push_back(matViews);
}

void Scene::createMesh(tinygltf::Model& model, tinygltf::Mesh& mesh)
{
    int primIdx = -1;
    for (int i = 0; i < mesh.primitives.size(); i++)
    {
        // only consider first primitive that is a triangle mesh
        if (mesh.primitives[i].mode == 4)
        {
            primIdx = i;
            break;
        }
    }
    if (primIdx == -1)
        throw std::runtime_error("unsupported GLTF mesh primitive mode!");

    tinygltf::Primitive& prim = mesh.primitives[primIdx];

    // TODO error checks
    // TODO different GLTF data formats
    tinygltf::Accessor& indexAccessor = model.accessors[prim.indices];
    tinygltf::Accessor& positionAccessor = model.accessors[prim.attributes["POSITION"]];
    tinygltf::Accessor& normalAccessor = model.accessors[prim.attributes["NORMAL"]];
    tinygltf::Accessor& texCoordAccessor = model.accessors[prim.attributes["TEXCOORD_0"]];

    tinygltf::BufferView& indexView = model.bufferViews[indexAccessor.bufferView];
    tinygltf::BufferView& positionView = model.bufferViews[positionAccessor.bufferView];
    tinygltf::BufferView& normalView = model.bufferViews[normalAccessor.bufferView];
    tinygltf::BufferView& texCoordView = model.bufferViews[texCoordAccessor.bufferView];

    tinygltf::Buffer& indexBuf = model.buffers[indexView.buffer];
    tinygltf::Buffer& positionBuf = model.buffers[positionView.buffer];
    tinygltf::Buffer& normalBuf = model.buffers[normalView.buffer];
    tinygltf::Buffer& texCoordBuf = model.buffers[texCoordView.buffer];

    // TODO don't duplicate or allocate too much data unnecessarily
    Mesh m;
    m.indexCount = static_cast<uint32_t>(indexAccessor.count);
    {
        std::shared_ptr<vk::Buffer> stagingBuf = m_rc->createBuffer(static_cast<VkDeviceSize>(indexView.byteLength), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* data = stagingBuf->map();
        // TODO missing byte stride
        // TODO accessor byte offset and count
        memcpy(data, indexBuf.data.data() + indexView.byteOffset, indexView.byteLength);
        stagingBuf->unmap();

        m.indexBuffer = m_rc->createBuffer(static_cast<VkDeviceSize>(indexView.byteLength), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
        m_rc->copyStagingBuffer(*m.indexBuffer, *stagingBuf, stagingBuf->m_size);
    }

    {
        std::shared_ptr<vk::Buffer> stagingBuf = m_rc->createBuffer(static_cast<VkDeviceSize>(positionView.byteLength), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* data = stagingBuf->map();
        memcpy(data, positionBuf.data.data() + positionView.byteOffset, positionView.byteLength);
        stagingBuf->unmap();

        m.positionBuffer = m_rc->createBuffer(static_cast<VkDeviceSize>(positionView.byteLength), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
        m_rc->copyStagingBuffer(*m.positionBuffer, *stagingBuf, stagingBuf->m_size);
    }

    {
        std::shared_ptr<vk::Buffer> stagingBuf = m_rc->createBuffer(static_cast<VkDeviceSize>(normalView.byteLength), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* data = stagingBuf->map();
        memcpy(data, normalBuf.data.data() + normalView.byteOffset, normalView.byteLength);
        stagingBuf->unmap();

        m.normalBuffer = m_rc->createBuffer(static_cast<VkDeviceSize>(normalView.byteLength), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
        m_rc->copyStagingBuffer(*m.normalBuffer, *stagingBuf, stagingBuf->m_size);
    }

    {
        std::shared_ptr<vk::Buffer> stagingBuf = m_rc->createBuffer(static_cast<VkDeviceSize>(texCoordView.byteLength), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* data = stagingBuf->map();
        memcpy(data, texCoordBuf.data.data() + texCoordView.byteOffset, texCoordView.byteLength);
        stagingBuf->unmap();

        m.texCoordBuffer = m_rc->createBuffer(static_cast<VkDeviceSize>(texCoordView.byteLength), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
        m_rc->copyStagingBuffer(*m.texCoordBuffer, *stagingBuf, stagingBuf->m_size);
    }

    m.material = &m_materialViews[prim.material];

    m_meshes.push_back(m);
}

void Scene::createNode(tinygltf::Model& model, tinygltf::Node& node, Node* parent)
{
    if (node.mesh < 0)
        return;

    Node* n = new Node;
    n->parent = parent;
    n->mesh = &m_meshes[node.mesh];

    if (!node.matrix.empty())
    {
        n->localTransform = glm::make_mat4(node.matrix.data());
    }
    else
    {
        glm::mat4 T(1.0f), R(1.0f), S(1.0f);
        if (!node.translation.empty())
            T = glm::translate(glm::mat4(1.0f), glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
        if (!node.rotation.empty())
            R = glm::toMat4(glm::quat(static_cast<float>(node.rotation[0]), static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2]), static_cast<float>(node.rotation[3])));
        if (!node.scale.empty())
            S = glm::scale(glm::mat4(1.0f), glm::vec3(node.scale[0], node.scale[1], node.scale[2]));

        n->localTransform = T * R * S;
    }

    n->recursiveTransform = n->parent ? n->localTransform * n->parent->localTransform : n->localTransform;
    m_nodes.push_back(n);

    for (int c : node.children)
    {
        tinygltf::Node& child = model.nodes[c];
        createNode(model, child, n);
    }
}

vk::AccelerationStructure::AccelerationStructure(vk::RenderContext* rc, const Scene& scene) : m_rc(rc)
{
    buildBlases(scene);
    buildTlas(scene);
}

vk::AccelerationStructure::~AccelerationStructure()
{
    static PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(m_rc->getDevice(), "vkDestroyAccelerationStructureKHR"));

    for (const auto& AS : m_blases)
        vkDestroyAccelerationStructureKHR(m_rc->getDevice(), AS.second, nullptr);
    vkDestroyAccelerationStructureKHR(m_rc->getDevice(), m_tlas, nullptr);
}

void vk::AccelerationStructure::buildBlases(const Scene& scene)
{
    static PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(m_rc->getDevice(), "vkGetAccelerationStructureBuildSizesKHR"));
    static PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(m_rc->getDevice(), "vkCreateAccelerationStructureKHR"));

    for (const Mesh& m : scene.m_meshes)
    {
        // TODO do we have to ensure buffers we get device addresses for are actually in device memory?
        VkBufferDeviceAddressInfo addrInfo{};
        addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addrInfo.buffer = *m.positionBuffer;
        VkDeviceOrHostAddressConstKHR vertexBufferAddr;
        vertexBufferAddr.deviceAddress = vkGetBufferDeviceAddress(m_rc->getDevice(), &addrInfo);

        addrInfo.buffer = *m.indexBuffer;
        VkDeviceOrHostAddressConstKHR indexBufferAddr;
        indexBufferAddr.deviceAddress = vkGetBufferDeviceAddress(m_rc->getDevice(), &addrInfo);

        // TODO different data formats
        VkAccelerationStructureGeometryTrianglesDataKHR trianglesData{};
        trianglesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        trianglesData.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        trianglesData.vertexData = vertexBufferAddr;
        trianglesData.maxVertex = (m.positionBuffer->m_size / 12u) - 1u;
        trianglesData.vertexStride = 12u;
        trianglesData.indexType = VK_INDEX_TYPE_UINT16;
        trianglesData.indexData = indexBufferAddr;

        VkAccelerationStructureGeometryDataKHR geomData{};
        geomData.triangles = trianglesData;

        VkAccelerationStructureGeometryKHR geom{};
        geom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geom.geometry = geomData;
        geom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.geometryCount = 1u;
        buildInfo.pGeometries = &geom;

        uint32_t primCount = m.indexCount / 3u;
        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
        vkGetAccelerationStructureBuildSizesKHR(m_rc->getDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primCount, &sizeInfo);

        std::shared_ptr<vk::Buffer> ASBuf = m_rc->createBuffer(sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);

        VkAccelerationStructureCreateInfoKHR ASInfo{};
        ASInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        ASInfo.buffer = *ASBuf;
        ASInfo.offset = 0u;
        ASInfo.size = sizeInfo.accelerationStructureSize;
        ASInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

        VkAccelerationStructureKHR AS;
        VK_CHECK(vkCreateAccelerationStructureKHR(m_rc->getDevice(), &ASInfo, nullptr, &AS), "failed to create BLAS!");

        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.dstAccelerationStructure = AS;

        std::shared_ptr<vk::Buffer> scratchBuf = m_rc->createBuffer(sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u, m_rc->m_ASProperties.minAccelerationStructureScratchOffsetAlignment);

        addrInfo.buffer = *scratchBuf;
        VkDeviceOrHostAddressKHR scratchBufferAddr;
        scratchBufferAddr.deviceAddress = vkGetBufferDeviceAddress(m_rc->getDevice(), &addrInfo);

        buildInfo.scratchData = scratchBufferAddr;

        VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
        rangeInfo.primitiveCount = primCount;
        rangeInfo.primitiveOffset = 0u;
        rangeInfo.firstVertex = 0u;

        m_rc->buildAS(buildInfo, &rangeInfo);

        m_blasBuffers.push_back(ASBuf);
        m_blases[&m] = AS;
    }
}

void vk::AccelerationStructure::buildTlas(const Scene& scene)
{
    static PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(m_rc->getDevice(), "vkGetAccelerationStructureBuildSizesKHR"));
    static PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(m_rc->getDevice(), "vkCreateAccelerationStructureKHR"));
    static PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(m_rc->getDevice(), "vkGetAccelerationStructureDeviceAddressKHR"));

    std::vector<VkAccelerationStructureInstanceKHR> instances;
    instances.reserve(scene.m_nodes.size());
    for (int i = 0; i < scene.m_nodes.size(); i++)
    {
        Node* n = scene.m_nodes[i];
        VkTransformMatrixKHR transform;
        for (int row = 0; row < 3; row++)
        {
            for (int column = 0; column < 4; column++)
            {
                // glm matrices are column-major
                transform.matrix[row][column] = n->recursiveTransform[column][row];
            }
        }

        VkAccelerationStructureDeviceAddressInfoKHR ASAddr{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
        ASAddr.accelerationStructure = m_blases[n->mesh];

        VkAccelerationStructureInstanceKHR instance{};
        instance.transform = transform;
        instance.instanceCustomIndex = i;
        instance.mask = 0xFF;
        instance.accelerationStructureReference = vkGetAccelerationStructureDeviceAddressKHR(m_rc->getDevice(), &ASAddr);

        instances.push_back(std::move(instance));
    }

    std::shared_ptr<Buffer> instancesDataBuf = m_rc->createBuffer(sizeof(VkAccelerationStructureInstanceKHR) * instances.size(), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 8u);
    void* data = instancesDataBuf->map();
    memcpy(data, instances.data(), sizeof(VkAccelerationStructureInstanceKHR) * instances.size());
    instancesDataBuf->unmap();

    VkBufferDeviceAddressInfo addrInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
    addrInfo.buffer = *instancesDataBuf;
    VkAccelerationStructureGeometryInstancesDataKHR instancesData{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
    instancesData.data.deviceAddress = vkGetBufferDeviceAddress(m_rc->getDevice(), &addrInfo);
    VkAccelerationStructureGeometryDataKHR geomData{};
    geomData.instances = instancesData;

    VkAccelerationStructureGeometryKHR geom{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geom.geometry = geomData;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.geometryCount = 1u;
    buildInfo.pGeometries = &geom;

    uint32_t instanceCount = static_cast<uint32_t>(instances.size());
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(m_rc->getDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &instanceCount, &sizeInfo);

    m_tlasBuffer = m_rc->createBuffer(sizeInfo.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);

    VkAccelerationStructureCreateInfoKHR ASInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    ASInfo.buffer = *m_tlasBuffer;
    ASInfo.offset = 0u;
    ASInfo.size = sizeInfo.accelerationStructureSize;
    ASInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    VK_CHECK(vkCreateAccelerationStructureKHR(m_rc->getDevice(), &ASInfo, nullptr, &m_tlas), "failed to create TLAS!");

    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.dstAccelerationStructure = m_tlas;

    std::shared_ptr<vk::Buffer> scratchBuf = m_rc->createBuffer(sizeInfo.buildScratchSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u, m_rc->m_ASProperties.minAccelerationStructureScratchOffsetAlignment);

    addrInfo.buffer = *scratchBuf;
    VkDeviceOrHostAddressKHR scratchBufferAddr;
    scratchBufferAddr.deviceAddress = vkGetBufferDeviceAddress(m_rc->getDevice(), &addrInfo);

    buildInfo.scratchData = scratchBufferAddr;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = instanceCount;
    rangeInfo.primitiveOffset = 0u;

    m_rc->buildAS(buildInfo, &rangeInfo);
}
