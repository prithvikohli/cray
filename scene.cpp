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

    VkImageCreateInfo stagingImageInfo{};
    stagingImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    stagingImageInfo.imageType = VK_IMAGE_TYPE_2D;
    stagingImageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    stagingImageInfo.mipLevels = 1u;
    stagingImageInfo.arrayLayers = 1u;
    stagingImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    stagingImageInfo.tiling = VK_IMAGE_TILING_LINEAR;
    stagingImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    stagingImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    stagingImageInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.mipLevels = 1u;
    imageInfo.arrayLayers = 1u;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    {
        stagingImageInfo.extent = { static_cast<uint32_t>(albedoImg.width), static_cast<uint32_t>(albedoImg.height), 1u};
        std::shared_ptr<vk::Image> stagingImg = m_rc->createImage(stagingImageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* data = stagingImg->map();
        memcpy(data, albedoImg.image.data(), albedoImg.image.size());
        stagingImg->unmap();

        imageInfo.extent = stagingImageInfo.extent;
        mat.albedo = m_rc->createImage(imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);

        m_rc->copyImage(*stagingImg, *mat.albedo);
    }

    {
        stagingImageInfo.extent = { static_cast<uint32_t>(metallicRoughnessImg.width), static_cast<uint32_t>(metallicRoughnessImg.height), 1u };
        std::shared_ptr<vk::Image> stagingImg = m_rc->createImage(stagingImageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* data = stagingImg->map();
        memcpy(data, metallicRoughnessImg.image.data(), metallicRoughnessImg.image.size());
        stagingImg->unmap();

        imageInfo.extent = stagingImageInfo.extent;
        mat.metallicRoughness = m_rc->createImage(imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);

        m_rc->copyImage(*stagingImg, *mat.metallicRoughness);
    }

    {
        stagingImageInfo.extent = { static_cast<uint32_t>(normalImg.width), static_cast<uint32_t>(normalImg.height), 1u };
        std::shared_ptr<vk::Image> stagingImg = m_rc->createImage(stagingImageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* data = stagingImg->map();
        memcpy(data, normalImg.image.data(), normalImg.image.size());
        stagingImg->unmap();

        imageInfo.extent = stagingImageInfo.extent;
        mat.normal = m_rc->createImage(imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);

        m_rc->copyImage(*stagingImg, *mat.normal);
    }

    {
        stagingImageInfo.extent = { static_cast<uint32_t>(emissiveImg.width), static_cast<uint32_t>(emissiveImg.height), 1u };
        std::shared_ptr<vk::Image> stagingImg = m_rc->createImage(stagingImageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* data = stagingImg->map();
        memcpy(data, emissiveImg.image.data(), emissiveImg.image.size());
        stagingImg->unmap();

        imageInfo.extent = stagingImageInfo.extent;
        mat.emissive = m_rc->createImage(imageInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);

        m_rc->copyImage(*stagingImg, *mat.emissive);
    }

    m_materials.push_back(mat);
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

        m.indexBuffer = m_rc->createBuffer(static_cast<VkDeviceSize>(indexView.byteLength), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
        m_rc->copyBuffer(*stagingBuf, *m.indexBuffer);
    }

    {
        std::shared_ptr<vk::Buffer> stagingBuf = m_rc->createBuffer(static_cast<VkDeviceSize>(positionView.byteLength), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* data = stagingBuf->map();
        memcpy(data, positionBuf.data.data() + positionView.byteOffset, positionView.byteLength);
        stagingBuf->unmap();

        m.positionBuffer = m_rc->createBuffer(static_cast<VkDeviceSize>(positionView.byteLength), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
        m_rc->copyBuffer(*stagingBuf, *m.positionBuffer);
    }

    {
        std::shared_ptr<vk::Buffer> stagingBuf = m_rc->createBuffer(static_cast<VkDeviceSize>(normalView.byteLength), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* data = stagingBuf->map();
        memcpy(data, normalBuf.data.data() + normalView.byteOffset, normalView.byteLength);
        stagingBuf->unmap();

        m.normalBuffer = m_rc->createBuffer(static_cast<VkDeviceSize>(normalView.byteLength), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
        m_rc->copyBuffer(*stagingBuf, *m.normalBuffer);
    }

    {
        std::shared_ptr<vk::Buffer> stagingBuf = m_rc->createBuffer(static_cast<VkDeviceSize>(texCoordView.byteLength), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        void* data = stagingBuf->map();
        memcpy(data, texCoordBuf.data.data() + texCoordView.byteOffset, texCoordView.byteLength);
        stagingBuf->unmap();

        m.texCoordBuffer = m_rc->createBuffer(static_cast<VkDeviceSize>(texCoordView.byteLength), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0u, 0u);
        m_rc->copyBuffer(*stagingBuf, *m.texCoordBuffer);
    }

    m.material = &m_materials[prim.material];

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
