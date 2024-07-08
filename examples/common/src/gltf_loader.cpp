#define TINYGLTF_IMPLEMENTATION
#include <spdlog/spdlog.h>

#include "gltf/GltfLoader.hpp"
#include <Texture.h>
#include <Vertex.h>

#include <tuple>
#include <stdexcept>
#include <numeric>

constexpr uint32_t MegaBytes =  1024 * 1024;
constexpr VkDeviceSize stagingBufferSize = 128 * MegaBytes;

VkFormat getFormat(const tinygltf::Image& image) {
    return VK_FORMAT_UNDEFINED;
}

void tinyGltfLoad(tinygltf::Model& model, const std::string& path) {
    std::string err;
    std::string warn;

    tinygltf::TinyGLTF loader;

    auto successLoad = loader.LoadASCIIFromFile(&model, &err, &warn, path);

    if(!warn.empty()) {
        spdlog::warn("Warning loading gltf model: {}", path);
    }

    if(!err.empty()) {
        spdlog::error("Error loading gltf model: {}", path);
    }

    if(!successLoad) {
        spdlog::info("unable to load gltf model: {}", path);
        throw std::runtime_error{ fmt::format("unable to load gltf model: {}", path) };
    }
}


std::tuple<size_t, size_t, size_t, size_t> primitiveCounts(const tinygltf::Model& model) {
    size_t numVertices = 0;
    size_t numIndices = 0;
    size_t numPrimitives = 0;
    for(const auto& mesh : model.meshes) {
        for(const auto& primitive : mesh.primitives) {
            ++numPrimitives;
            numVertices += model.accessors[primitive.attributes.at("POSITION")].count;
            numIndices += model.accessors[primitive.indices].count;
        }
    }

    size_t numMeshInstances = 0;
    for(const auto& node : model.nodes) {
        numMeshInstances += node.mesh == -1 ? 0 : node.mesh;
     }

    return std::make_tuple(numMeshInstances, numPrimitives, numVertices, numIndices);
}

size_t getNumVertices(const tinygltf::Model& model, const tinygltf::Mesh& mesh) {
    size_t numVertices = 0;
    for(const auto& primitive : mesh.primitives) {
        numVertices += model.accessors[primitive.attributes.at("POSITION")].count;
    }
    return numVertices;
}

size_t getNumVertices(const tinygltf::Model& model, const tinygltf::Primitive& primitive) {
    return model.accessors[primitive.attributes.at("POSITION")].count;

}
template<typename T>
std::span<const T> getAttributeData(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const std::string& attribute) {
    if(!primitive.attributes.contains(attribute)){
        return {};
    }

    auto& accessor = model.accessors[primitive.attributes.at(attribute)];

    if(accessor.count == 0) {
        return {};
    }

    auto& bufferView = model.bufferViews[accessor.bufferView];
    auto& buffer = model.buffers[bufferView.buffer];

    auto start = reinterpret_cast<const T*>(buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);
    auto size = bufferView.byteLength/sizeof(T);

    return { start, size };
}

std::span<const uint16_t> getIndices(const tinygltf::Model& model, const tinygltf::Primitive& primitive) {
    auto& accessor = model.accessors[primitive.indices];

    if(accessor.count == 0) {
        return {};
    }

    auto& bufferView = model.bufferViews[accessor.bufferView];
    auto& buffer = model.buffers[bufferView.buffer];

    auto start = reinterpret_cast<const uint16_t *>(buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);
    auto size = accessor.count;

    return { start, size };
}

std::tuple<glm::vec3, glm::vec3> computeBounds(const tinygltf::Model& model) {
    glm::vec3 bMin{ std::numeric_limits<float>::max() };
    glm::vec3 bMax{ std::numeric_limits<float>::lowest() };

    for(const auto& accessor : model.accessors) {
        if(accessor.minValues.size() == 3) {
            const auto& mv = accessor.minValues;
            bMin = glm::min(bMin, {mv[0], mv[1], mv[2]});
        }
        if(accessor.maxValues.size() == 3) {
            const auto& mv = accessor.maxValues;
            bMax = glm::max(bMax, {mv[0], mv[1], mv[2]});
        }
    }

    return std::make_tuple(bMin, bMax);
}

gltf::Loader::Loader(VulkanDevice *device, VulkanDescriptorPool* descriptorPool, BindlessDescriptor* bindlessDescriptor, size_t reserve)
: _device{ device }
, _descriptorPool{ descriptorPool }
, _bindlessDescriptor{ bindlessDescriptor }
, _pendingModels(reserve)
, _texturesUploads(100)
{}

void gltf::Loader::start() {
    initPlaceHolders();
    createDescriptorSetLayout();
    _stagingBuffer = _device->createStagingBuffer(stagingBufferSize, {*_device->queueFamilyIndex.transfer });
    _fence = _device->createFence();
    _running = true;
    _thread = std::thread{[this] { execLoop(); }};
    spdlog::info("GLTF model loader started");
}

std::shared_ptr<gltf::Model> gltf::Loader::load(const std::filesystem::path &path) {
    if(_pendingModels.full()) {
        return {};
    }

    auto gltf  = std::make_unique<tinygltf::Model>();
    tinyGltfLoad(*gltf, path.string());

    const auto numMaterials = gltf->materials.size();
    const auto [numMeshInstances, numMeshes, numVertices, numIndices] = primitiveCounts(*gltf);
    auto [bMin, bMax] = computeBounds(*gltf);

    auto pendingModel = std::make_shared<PendingModel>();
    pendingModel->bindlessDescriptor = _bindlessDescriptor;
    pendingModel->textureBindingOffset = _bindlessDescriptor->reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, gltf->textures.size());
    pendingModel->device = _device;
    pendingModel->gltf = std::move(gltf);

    pendingModel->model = std::make_shared<gltf::Model>();
    pendingModel->model->numMeshes = numMeshes;
    pendingModel->model->numTextures = pendingModel->gltf->textures.size();
    pendingModel->meshes.reset(numMeshes);

    pendingModel->model->vertexBuffer = _device->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
            , VMA_MEMORY_USAGE_GPU_ONLY, sizeof(Vertex) * numVertices, "");

    pendingModel->model->indexBuffer = _device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
            , VMA_MEMORY_USAGE_GPU_ONLY, sizeof(uint16_t) * numIndices, "");

    pendingModel->model->draw.gpu = _device->createBuffer(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, numMeshes * sizeof(VkDrawIndexedIndirectCommand));
    pendingModel->model->draw.count = 0;

    pendingModel->model->meshBuffer = _device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(gltf::MeshData) * numMeshes);
    pendingModel->model->materialBuffer = _device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(gltf::MeshData) * numMaterials);

    pendingModel->model->bounds.min = bMin;
    pendingModel->model->bounds.max = bMax;
    pendingModel->model->descriptorSet = _descriptorPool->allocate( { _descriptorSetLayout } ).front();
    
    auto writes = initializers::writeDescriptorSets<2>();
    writes[0].dstSet = pendingModel->model->descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo meshBufferInfo{ pendingModel->model->meshBuffer, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &meshBufferInfo;

    writes[1].dstSet = pendingModel->model->descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo materialBufferInfo{ pendingModel->model->materialBuffer, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &materialBufferInfo;

    _device->updateDescriptorSets(writes);

    for(auto i = 0; i < pendingModel->gltf->textures.size(); ++i) {
        const auto bindingId = static_cast<uint32_t>(i + pendingModel->textureBindingOffset);
        _bindlessDescriptor->update({ &_placeHolderTexture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, bindingId});

        const auto& texture = pendingModel->gltf->textures[i];
        // TODO create sampler
        TextureUploadRequest request{ pendingModel->model, pendingModel->gltf->images[texture.source].uri, bindingId, pendingModel->gltf->images[texture.source] };
        _texturesUploads.push(request);
    }


    _pendingModels.push(pendingModel);
    _loadRequest.notify_one();

    return pendingModel->model;
}

void gltf::Loader::execLoop() {
    while(_running) {
        using namespace std::chrono_literals;
        {
            std::unique_lock<std::mutex> lk{_mutex};
            _loadRequest.wait(lk, [&]{
                return !_pendingModels.empty()
                    || !_texturesUploads.empty()
                    || !_running;
            });
        }
        if(!_running) break;
        uploadMeshes();
        uploadTextures();
    }
    _pendingModels.reset(0);
    spdlog::info("GLTF async loader offline");
}

void gltf::Loader::stop() {
    _running = false;
//    auto sentinel = std::make_shared<PendingModel>();
//    _pendingModels.push(sentinel);
    _loadRequest.notify_one();
    _thread.join();
}

void gltf::Loader::uploadMeshes() {
    const auto queue = _device->queues.transfer;
    const auto& commandPool = _device->createCommandPool(*_device->queueFamilyIndex.transfer, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    auto commandBuffer = commandPool.allocate();

    while(!_pendingModels.empty()){
        auto pending = _pendingModels.pop();
        auto model = pending->model;
        auto& meshes = pending->gltf->meshes;

        uint32_t vertexOffset = 0;
        uint32_t firstIndex = 0;
        std::array<VkBufferCopy, 2> regions{};

        auto meshId = 0u;
        for(auto& gltfMesh : meshes) {

            for(const auto& primitive : gltfMesh.primitives) {

                const auto numVertices = getNumVertices(*pending->gltf, primitive);
                std::vector<Vertex> vertices;
                vertices.reserve(numVertices);

                auto positions = getAttributeData<glm::vec3>(*pending->gltf, primitive, "POSITION");
                auto normals = getAttributeData<glm::vec3>(*pending->gltf, primitive, "NORMAL");
                auto tangents = getAttributeData<glm::vec4>(*pending->gltf, primitive, "TANGENT");
                auto uvs = getAttributeData<glm::vec2>(*pending->gltf, primitive, "TEXCOORD_0");

                for(auto i = 0; i < numVertices; ++i) {
                    Vertex vertex{};
                    vertex.position = glm::vec4(positions[i], 1);
                    vertex.normal = normals[i];
                    vertex.tangent = tangents.empty() ? glm::vec3(0) : tangents[i].xyz();
                    vertex.bitangent = tangents.empty() ? glm::vec3(0) : glm::cross(normals[i], tangents[i].xyz()) * tangents[i].w;
                    vertex.uv = uvs.empty() ? glm::vec2(0) : uvs[i];
                    vertices.push_back(vertex);
                }
                auto pIndices = getIndices(*pending->gltf, primitive);
                std::vector<uint16_t> indices{pIndices.begin(), pIndices.end()};

                regions[0].size = BYTE_SIZE(vertices);
                _stagingBuffer.copy(vertices, 0);

                regions[1].srcOffset = regions[0].size;
                regions[1].size = BYTE_SIZE(indices);
                _stagingBuffer.copy(indices, regions[1].srcOffset);

                _fence.reset();
                auto beginInfo = initializers::commandBufferBeginInfo();
                vkBeginCommandBuffer(commandBuffer, &beginInfo);

                assert(model->vertexBuffer.size >= regions[0].dstOffset);
                vkCmdCopyBuffer(commandBuffer, _stagingBuffer, model->vertexBuffer, 1, &regions[0]);

                assert(model->indexBuffer.size >= regions[1].dstOffset);
                vkCmdCopyBuffer(commandBuffer, _stagingBuffer, model->indexBuffer, 1, &regions[1]);

                std::vector<VkBufferMemoryBarrier> barriers(2);
                barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barriers[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
                barriers[0].srcQueueFamilyIndex = _device->queueFamilyIndex.transfer.value();
                barriers[0].dstQueueFamilyIndex = _device->queueFamilyIndex.graphics.value();
                barriers[0].offset = regions[0].dstOffset;
                barriers[0].buffer = model->vertexBuffer;
                barriers[0].size = regions[0].size;

                barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barriers[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
                barriers[1].srcQueueFamilyIndex = _device->queueFamilyIndex.transfer.value();
                barriers[1].dstQueueFamilyIndex = _device->queueFamilyIndex.graphics.value();
                barriers[1].offset = regions[1].dstOffset;
                barriers[1].buffer = model->indexBuffer;
                barriers[1].size = regions[1].size;

                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0,
                                     VK_NULL_HANDLE, COUNT(barriers), barriers.data(), 0, VK_NULL_HANDLE);

                vkEndCommandBuffer(commandBuffer);

                VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &commandBuffer;
                vkQueueSubmit(queue, 1, &submitInfo, _fence.fence);

                _fence.wait();

                Mesh mesh{
                        .meshId = static_cast<uint32_t>(meshId++),
                        .indexCount = static_cast<uint32_t>(indices.size()),
                        .instanceCount = 1u,
                        .firstIndex = firstIndex,
                        .vertexOffset = vertexOffset,
                        .firstInstance = 0,
                        .materialId = primitive.material == -1 ? 0 : static_cast<uint32_t>(primitive.material)
                };
                pending->meshes.push(mesh);

                firstIndex += mesh.indexCount;
                vertexOffset += vertices.size();
                regions[0].dstOffset += regions[0].size;
                regions[1].dstOffset += regions[1].size;
            }

        }
        pending->updateDrawState();
        pending->uploadMaterials();
    }
}

void gltf::Loader::createDescriptorSetLayout() {
    _descriptorSetLayout = 
        _device->descriptorSetLayoutBuilder()
            .name("gltf_model_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
            .createLayout();
}

void gltf::Loader::uploadTextures() {

    std::vector<UploadedTexture> uploadedTextures;
    _device->transferCommandPool().oneTimeCommand([&](auto commandBuffer){
        VkImageCreateInfo createInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        createInfo.imageType = VK_IMAGE_TYPE_2D;
        createInfo.format = VK_FORMAT_R8G8B8A8_UNORM; // TODO derive format i.e. getFormat(imate);
        createInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.mipLevels = 1; // TODO generate mip levels;
        createInfo.arrayLayers = 1;
        createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        std::vector<VkImageMemoryBarrier> prepTransferBarriers( _texturesUploads.size(), { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER });
        std::vector<VkImageMemoryBarrier> prepReadBarriers( _texturesUploads.size(), { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER });
        int textureId = 0;

        std::vector<VkBufferImageCopy> regions( _texturesUploads.size(), {0, 0, 0});
        VkDeviceSize bufferOffset = 0;
        while(!_texturesUploads.empty()) {
            const auto request = _texturesUploads.pop();

            if(request.image.image.size() + bufferOffset >= _stagingBuffer.size) {
                _texturesUploads.push(request);
                break;
            }

            UploadedTexture uploadedTexture{ request.model, request.bindingId };
            auto& texture = uploadedTexture.texture;
            texture.width = static_cast<uint32_t>(request.image.width);
            texture.height = static_cast<uint32_t>(request.image.height);
            texture.levels = static_cast<uint32_t>(std::log2(std::max(texture.width, texture.height))) + 1;
            createInfo.mipLevels = texture.levels;

            createInfo.extent = { texture.width, texture.height, 1 };

            texture.image = _device->createImage(createInfo);
            _device->setName<VK_OBJECT_TYPE_IMAGE>(request.uri, texture.image.image);

            VkImageSubresourceRange subresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, texture.levels, 0, 1};
            texture.imageView = texture.image.createView(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);

            _stagingBuffer.copy(request.image.image, bufferOffset);  // TODO retrieve data from BufferView if uri.empty() == true

            prepTransferBarriers[textureId].image = texture.image.image;
            prepTransferBarriers[textureId].subresourceRange = subresourceRange;
            prepTransferBarriers[textureId].srcAccessMask = VK_ACCESS_NONE;
            prepTransferBarriers[textureId].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            prepTransferBarriers[textureId].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            prepTransferBarriers[textureId].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &prepTransferBarriers[textureId]);

            regions[textureId].bufferOffset = bufferOffset;
            regions[textureId].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            regions[textureId].imageSubresource.mipLevel = 0;
            regions[textureId].imageSubresource.baseArrayLayer = 0;
            regions[textureId].imageSubresource.layerCount = 1;
            regions[textureId].imageOffset = {0, 0, 0};
            regions[textureId].imageExtent = { texture.width, texture.height, 1 };
            vkCmdCopyBufferToImage(commandBuffer, _stagingBuffer.buffer, texture.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &regions[textureId]);

            prepReadBarriers[textureId].image = texture.image.image;
            prepReadBarriers[textureId].subresourceRange = subresourceRange;
            prepReadBarriers[textureId].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            prepReadBarriers[textureId].dstAccessMask = VK_ACCESS_NONE;
            prepReadBarriers[textureId].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            prepReadBarriers[textureId].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            prepReadBarriers[textureId].srcQueueFamilyIndex = *_device->queueFamilyIndex.transfer;
            prepReadBarriers[textureId].dstQueueFamilyIndex = *_device->queueFamilyIndex.graphics;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_NONE, 0, 0, nullptr, 0, nullptr, 1, &prepReadBarriers[textureId]);

            uploadedTextures.push_back(std::move(uploadedTexture));
            ++textureId;
            bufferOffset += request.image.image.size();
        }

        using namespace std::chrono_literals;
        for(auto& texture : uploadedTextures) {
            _bindlessDescriptor->update({ &texture.texture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, texture.bindingId });
            texture.model->textures.push_back(std::move(texture.texture));
        }
    });
}

void gltf::Loader::initPlaceHolders() {
    textures::color(*_device, _placeHolderTexture, glm::vec3(1), {32, 32});
    textures::normalMap(*_device, _placeHolderNormalTexture, {32, 32});
}