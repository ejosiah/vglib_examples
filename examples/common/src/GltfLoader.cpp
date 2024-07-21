#include "gltf/GltfLoader.hpp"
#include "Vertex.h"
#include <spdlog/spdlog.h>
#include <AbstractCamera.hpp>
#include <utility>
#include <xforms.h>

namespace gltf {

    static constexpr const char* KHR_materials_transmission = "KHR_materials_transmission";
    static constexpr const char* KHR_materials_volume = "KHR_materials_volume";
    static constexpr const char* KHR_materials_ior = "KHR_materials_ior";
    static constexpr const char* KHR_materials_dispersion = "KHR_materials_dispersion";
    static constexpr const char* KHR_lights_punctual = "KHR_lights_punctual";
    static constexpr const char* KHR_materials_emissive_strength = "KHR_materials_emissive_strength";
    static constexpr const char* KHR_materials_clearcoat = "KHR_materials_clearcoat";
    static constexpr const char* KHR_texture_transform = "KHR_texture_transform";
    static constexpr const char* KHR_materials_sheen = "KHR_materials_sheen";
    static constexpr const char* KHR_materials_unlit = "KHR_materials_unlit";

    static const MaterialData NullMaterial{ .baseColor{std::numeric_limits<float>::quiet_NaN()} };

    struct Counts {
        struct { size_t u8{}; size_t u16{}; size_t u32{}; size_t count() const { return u8 + u16 + u32; }} instances;
        struct { size_t u8{}; size_t u16{}; size_t u32{}; size_t count() const { return u8 + u16 + u32; }} indices;
        size_t primitives{};
        size_t vertices{};
        size_t numLights{};
        size_t numLightInstances{};
    };

    glm::vec2 vec2From(const std::vector<double>& v);
    glm::vec2 vec2From(const tinygltf::Value& value);
    glm::vec3 vec3From(const std::vector<double>& v);
    glm::vec3 vec3From(const tinygltf::Value& value);
    glm::vec4 vec4From(const std::vector<double>& v);
    glm::quat quaternionFrom(const std::vector<double>& q);
    glm::mat4 getTransformation(const tinygltf::Node& node);
    int alphaModeToIndex(const std::string& mode);

    size_t getNumVertices(const tinygltf::Model& model, const tinygltf::Primitive& primitive);

    size_t getNumVertices(const tinygltf::Model& model, const tinygltf::Mesh& mesh);

    std::tuple<size_t, size_t, size_t> getNumIndices(const tinygltf::Model& model, const tinygltf::Mesh& mesh);

    void tinyGltfLoad(tinygltf::Model& model, const std::string& path);

    Counts getCounts(const tinygltf::Model& model);

    std::vector<glm::mat4> computeTransforms(const tinygltf::Model& model);

    std::vector<glm::mat4> computePlaceHolders(const tinygltf::Model& model, const std::vector<glm::mat4>& transforms);

    std::tuple<glm::vec3, glm::vec3> computeBounds(const tinygltf::Model& model, const tinygltf::Mesh& mesh);

    std::tuple<glm::vec3, glm::vec3> computeBounds(const tinygltf::Model& model, const std::vector<glm::mat4>& transforms);

    std::vector<Camera> getCameras(const tinygltf::Model& model);

    ComponentType getComponentType(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const std::string& attributeName);

    void computeOffsets(const std::shared_ptr<PendingModel>& pending);

    bool isFloat(VkFormat format) {
        auto itr = std::find_if(Loader::floatFormats.begin(), Loader::floatFormats.end(), [&](auto aFormat){ return format == aFormat; });
        return itr != Loader::floatFormats.end();
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


    template<typename IndexType>
    std::span<const IndexType> getIndices(const tinygltf::Model& model, const tinygltf::Primitive& primitive) {
        auto& accessor = model.accessors[primitive.indices];

        if(accessor.count == 0) {
            return {};
        }

        auto& bufferView = model.bufferViews[accessor.bufferView];
        auto& buffer = model.buffers[bufferView.buffer];

        auto start = reinterpret_cast<const IndexType *>(buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);
        auto size = accessor.count;

        return { start, size };
    }

    template<typename indexType>
    void calculateTangents(std::vector<VertexMultiAttributes>& vertices, std::vector<indexType> indices) {
        for(int i = 0; i < indices.size(); i+= 3){
            auto& v0 = vertices[indices[i]];
            auto& v1 = vertices[indices[i+1]];
            auto& v2 = vertices[indices[i+2]];

            auto e1 = v1.position.xyz() - v0.position.xyz();
            auto e2 = v2.position.xyz() - v0.position.xyz();

            auto du1 = v1.uv[0].x - v0.uv[0].x;
            auto dv1 = v1.uv[0].y - v0.uv[0].y;
            auto du2 = v2.uv[0].x - v0.uv[0].x;
            auto dv2 = v2.uv[0].y - v0.uv[0].y;

            auto d = 1.f/(du1 * dv2 - dv1 * du2);

            glm::vec3 tn{0};
            tn.x = d * (dv2 * e1.x - dv1 * e2.x);
            tn.y = d * (dv2 * e1.y - dv1 * e2.y);
            tn.z = d * (dv2 * e1.z - dv1 * e2.z);

            glm::vec3 bn{0};
            bn.x = d * (du1 * e2.x - du2 * e1.x);
            bn.y = d * (du1 * e2.y - du2 * e1.y);
            bn.z = d * (du1 * e2.z - du2 * e1.z);

            v0.tangent = normalize(tn);
            v1.tangent = normalize(tn);
            v2.tangent = normalize(tn);

            v0.bitangent = normalize(bn);
            v1.bitangent = normalize(bn);
            v2.bitangent = normalize(bn);
        }
    }


    Loader::Loader(VulkanDevice *device, VulkanDescriptorPool *descriptorPool, BindlessDescriptor *bindlessDescriptor, size_t numWorkers)
    : _device{ device }
    , _descriptorPool{ descriptorPool }
    , _bindlessDescriptor{ bindlessDescriptor }
    , _pendingModels( 1024 )
    , _workerQueue(numWorkers, 1024)
    , _commandBufferQueue(1024)
    , _workerCount(numWorkers)
    , _barrierObjectPools(numWorkers, BufferMemoryBarrierPool(128) )
    , _imageMemoryBarrierObjectPools(numWorkers, ImageMemoryBarrierPool(128) )
    , _bufferCopyPool( numWorkers, BufferCopyPool(128))
    , _bufferImageCopyPool( numWorkers, BufferImageCopyPool(128))
    , _readyTextures(128)
    , _pendingTextureUploads(128)
    , _uploadedTextures(128)
    {}

    void Loader::start() {
        initPlaceHolders();
        createDescriptorSetLayout();
        initCommandPools();
        _fence = _device->createFence();
        _running = true;

        for(auto i = 0; i < _workerCount; ++i){
            std::thread worker{ [workerId = i, this]{ workerLoop(workerId); } };
            _workers.push_back(std::move(worker));
            _stagingBuffers.push_back(StagingBuffer{ _device, stagingBufferSize});
        }

        _coordinator = std::thread{ [this]{ coordinatorLoop(); } };
        spdlog::info("GLTF model loader started");
    }

    std::shared_ptr<Model> Loader::loadGltf(const std::filesystem::path &path) {
        if(_pendingModels.full()) {
            spdlog::warn("loader at capacity");
            return {};
        }

        auto gltf  = std::make_unique<tinygltf::Model>();
        tinyGltfLoad(*gltf, path.string());
        const auto counts = getCounts(*gltf);

        const auto textureBindingOffset = _bindlessDescriptor->reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, gltf->textures.size());


        auto model = std::make_shared<Model>();
        model->numMeshes = counts.instances.count();
        model->numTextures = gltf->textures.size();

        VkDeviceSize byteSize = sizeof(VertexMultiAttributes) * counts.vertices;
        model->vertices = _device->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_vertices", _modelId));

        byteSize =  sizeof(uint8_t) + sizeof(uint8_t) * counts.indices.u8;
        model->indices.u8.handle = _device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_indices_u8", _modelId));

        byteSize =  sizeof(uint16_t) + sizeof(uint16_t) * counts.indices.u16;
        model->indices.u16.handle = _device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_indices_u16", _modelId));

        byteSize = sizeof(uint32_t) + sizeof(uint32_t) * counts.indices.u32;
        model->indices.u32.handle = _device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT , VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_indices_u32", _modelId));

        byteSize = counts.instances.u8 * sizeof(VkDrawIndexedIndirectCommand) + sizeof(VkDrawIndexedIndirectCommand);
        model->draw.u8.handle = _device->createBuffer(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_draw_u8", _modelId));
        model->draw.u8.count = 0;

        byteSize = counts.instances.u16 * sizeof(VkDrawIndexedIndirectCommand) + sizeof(VkDrawIndexedIndirectCommand);
        model->draw.u16.handle = _device->createBuffer(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_draw_u16", _modelId));
        model->draw.u16.count = 0;

        byteSize = counts.instances.u32 * sizeof(VkDrawIndexedIndirectCommand) + sizeof(VkDrawIndexedIndirectCommand);
        model->draw.u32.handle = _device->createBuffer(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_draw_u32", _modelId));
        model->draw.u32.count = 0;

        byteSize = sizeof(MeshData) * counts.instances.u8 + sizeof(MeshData);
        model->meshes.u8.handle = _device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_instance_u8", _modelId));

        byteSize = sizeof(MeshData) * counts.instances.u16 + sizeof(MeshData);
        model->meshes.u16.handle = _device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_instance_u16", _modelId));

        byteSize = sizeof(MeshData) * counts.instances.u32 + sizeof(MeshData);
        model->meshes.u32.handle = _device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_instance_u32", _modelId));

        byteSize = sizeof(Light) + sizeof(Light) * counts.numLights;
        model->lights = _device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_lights", _modelId));

        byteSize = sizeof(LightInstance) + sizeof(LightInstance) * counts.numLightInstances;
        model->lightInstances = _device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_light_instances", _modelId));

        std::vector<MaterialData> materials(gltf->materials.size() + 1);
        std::vector<TextureInfo> textureInfos(materials.size() * NUM_TEXTURE_MAPPING, TextureInfo{ });
        
        auto back = materials.size() - 1; 
        materials[back] = NullMaterial;
        materials[back].textureInfoOffset = back;
        model->materials = _device->createDeviceLocalBuffer(materials.data(), BYTE_SIZE(materials), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        _device->setName<VK_OBJECT_TYPE_BUFFER>(fmt::format("mode{}_materials", _modelId), model->materials.buffer);
        
        model->textureInfos = _device->createDeviceLocalBuffer(textureInfos.data(), BYTE_SIZE(textureInfos), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        _device->setName<VK_OBJECT_TYPE_BUFFER>(fmt::format("mode{}_texture_infos", _modelId), model->textureInfos.buffer);

        auto transforms = computeTransforms(*gltf);
        auto [bMin, bMax] = computeBounds(*gltf, transforms);
        auto sets = _descriptorPool->allocate({_descriptorSetLayout, _descriptorSetLayout, _descriptorSetLayout, _materialDescriptorSetLayout });
        model->bounds.min = bMin;
        model->bounds.max = bMax;
        model->meshDescriptorSet.u8.handle = sets[0];
        model->meshDescriptorSet.u16.handle = sets[1];
        model->meshDescriptorSet.u32.handle = sets[2];
        model->materialDescriptorSet = sets[3];
        model->placeHolders =  computePlaceHolders(*gltf, transforms);
        model->numLights = counts.numLightInstances;
        model->cameras = getCameras(*gltf);
        model->_sourceDescriptorPool = _descriptorPool;
        model->_bindlessDescriptor = _bindlessDescriptor;
        model->_textureBindingOffset = textureBindingOffset;

        auto writes = initializers::writeDescriptorSets<7>();
        writes[0].dstSet = model->meshDescriptorSet.u8.handle;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        VkDescriptorBufferInfo meshBuffer8Info{ model->meshes.u8.handle, 0, VK_WHOLE_SIZE };
        writes[0].pBufferInfo = &meshBuffer8Info;

        writes[1].dstSet = model->meshDescriptorSet.u16.handle;
        writes[1].dstBinding = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        VkDescriptorBufferInfo meshBuffer16Info{ model->meshes.u16.handle, 0, VK_WHOLE_SIZE };
        writes[1].pBufferInfo = &meshBuffer16Info;

        writes[2].dstSet = model->meshDescriptorSet.u32.handle;
        writes[2].dstBinding = 0;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        VkDescriptorBufferInfo meshBufferInfo{ model->meshes.u32.handle, 0, VK_WHOLE_SIZE };
        writes[2].pBufferInfo = &meshBufferInfo;

        writes[3].dstSet = model->materialDescriptorSet;
        writes[3].dstBinding = 0;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].descriptorCount = 1;
        VkDescriptorBufferInfo materialBufferInfo{ model->materials, 0, VK_WHOLE_SIZE };
        writes[3].pBufferInfo = &materialBufferInfo;

        writes[4].dstSet = model->materialDescriptorSet;
        writes[4].dstBinding = 1;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].descriptorCount = 1;
        VkDescriptorBufferInfo lightBufferInfo{ model->lights, 0, VK_WHOLE_SIZE };
        writes[4].pBufferInfo = &lightBufferInfo;

        writes[5].dstSet = model->materialDescriptorSet;
        writes[5].dstBinding = 2;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].descriptorCount = 1;
        VkDescriptorBufferInfo lightInstanceBufferInfo{ model->lightInstances, 0, VK_WHOLE_SIZE };
        writes[5].pBufferInfo = &lightInstanceBufferInfo;

        writes[6].dstSet = model->materialDescriptorSet;
        writes[6].dstBinding = 3;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[6].descriptorCount = 1;
        VkDescriptorBufferInfo textureInfoBufferInfo{ model->textureInfos, 0, VK_WHOLE_SIZE };
        writes[6].pBufferInfo = &textureInfoBufferInfo;

        _device->updateDescriptorSets(writes);

        for(auto i = 0; i < gltf->textures.size(); ++i) {
            const auto bindingId = static_cast<uint32_t>(i + textureBindingOffset);
            _bindlessDescriptor->update({ &_placeHolderTexture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, bindingId});
        }

        auto pendingModel = std::make_shared<PendingModel>();
        pendingModel->device = _device;
        pendingModel->bindlessDescriptor = _bindlessDescriptor;
        pendingModel->textures.resize(model->numTextures);
        pendingModel->gltf = std::move(gltf);
        pendingModel->model = model;
        pendingModel->transforms = transforms;
        pendingModel->textureBindingOffset = textureBindingOffset;
        pendingModel->numLights = counts.numLights;

        _pendingModels.push(pendingModel);
        _coordinatorWorkAvailable.notify();

        return model;

    }

    std::shared_ptr<TextureUploadStatus> Loader::loadTexture(const std::filesystem::path &path, Texture &texture) {

        int width, height, channels;
        stbi_info(path.string().c_str(), &width, &height, &channels);

        channels = channels == 3 ? STBI_rgb_alpha : channels;
        auto format = texture.format == VK_FORMAT_UNDEFINED ? channelFormatMap.at(channels) : texture.format;
        VkDeviceSize imageSize = width * height * channels * textures::byteSize(format);

        auto& imageCreateInfo = texture.spec;
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.format = format;
        imageCreateInfo.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
        imageCreateInfo.mipLevels = !texture.lod ? 1 : static_cast<uint32_t>(std::log2(std::max(texture.width, texture.height))) + 1;
        imageCreateInfo.arrayLayers = texture.layers;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        texture.image = _device->createImage(imageCreateInfo, VMA_MEMORY_USAGE_GPU_ONLY);
        texture.image.size = imageSize;
        texture.width = width;
        texture.height = height;
        texture.depth = 1;
        texture.path = path.filename().string();
        texture.format = format;

        VkImageSubresourceRange subresourceRange;
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = imageCreateInfo.mipLevels;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = 1;

        texture.imageView = texture.image.createView(format, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);

        if(!texture.sampler.handle) {
            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = VK_FILTER_LINEAR;
            samplerInfo.minFilter = VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            samplerInfo.maxLod = imageCreateInfo.mipLevels - 1;
            texture.sampler = _device->createSampler(samplerInfo);
        }

        if(texture.bindingId != std::numeric_limits<uint32_t>::max()) {
            _bindlessDescriptor->update({ &_placeHolderTexture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, texture.bindingId});
        }

        auto status = std::make_shared<TextureUploadStatus>();
        status->texture = &texture;
        _pendingTextureUploads.push({ path.string(), channels, status});
        _coordinatorWorkAvailable.notify();

        return status;
    }

    void Loader::initPlaceHolders() {
        textures::color(*_device, _placeHolderTexture, glm::vec3(1), {32, 32});
        textures::normalMap(*_device, _placeHolderNormalTexture, {32, 32});
    }

    void Loader::createDescriptorSetLayout() {
        _descriptorSetLayout =
            _device->descriptorSetLayoutBuilder()
                .name("gltf_model_set_layout")
                .binding(0)
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
                .createLayout();

        _materialDescriptorSetLayout =
            _device->descriptorSetLayoutBuilder()
                .name("gltf_material_set_layout")
                .binding(0) // Materials
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
                .binding(1) // Lights
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
                .binding(2) // Light instances
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
                .binding(3) // texture infos
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
                .createLayout();
    }

    void Loader::coordinatorLoop() {
        std::stack<Task> completedTasks;
        std::vector<VkCommandBuffer> commandBuffers;

        while(_running) {
            _coordinatorWorkAvailable.wait([&]{
                return !_pendingModels.empty()
                || !_commandBufferQueue.empty()
                || !_pendingTextureUploads.empty()
                || !_running; });

            if(!_running) {
                _workerQueue.broadcast(StopWorkerTask{});
                _taskPending.notifyAll();
                for(auto& worker : _workers) {
                    worker.join();
                }
            }

            auto availableCommands = _commandBufferQueue.size();
            while(availableCommands > 0) {
                auto result = _commandBufferQueue.pop();
                commandBuffers.insert( commandBuffers.end(), result.commandBuffers.begin(), result.commandBuffers.end());
                completedTasks.push(std::move(result.task));
                --availableCommands;
            }

            if(!commandBuffers.empty()){
                spdlog::info("executing {} commandBuffers", commandBuffers.size());
                execute(commandBuffers);
                while(!completedTasks.empty()){
                    auto task = completedTasks.top();
                    onComplete(task);
                    completedTasks.pop();
                }
                _taskPending.notifyAll();
            }

            commandBuffers.clear();

            while(!_pendingModels.empty()) {
                auto pending = _pendingModels.pop();

                computeOffsets(pending);

                for(auto meshId = 0u; meshId < pending->gltf->meshes.size(); ++meshId) {
                    auto& mesh = pending->gltf->meshes[meshId];
                    _workerQueue.push(MeshUploadTask{ pending, mesh, meshId});
                }

                for(auto i = 0; i < pending->gltf->textures.size(); ++i) {
                    uint32_t bindingId = i + pending->textureBindingOffset;
                    auto& texture = pending->gltf->textures[i];
                    _workerQueue.push(GltfTextureUploadTask{pending, texture, bindingId, static_cast<uint32_t>(i) });
                }

                for(auto materialId = 0u; materialId < pending->gltf->materials.size(); ++materialId) {
                    auto& material = pending->gltf->materials[materialId];
                    MaterialUploadTask task{ .material = material, .pending = pending, .materialId = materialId, .textureOffset = pending->textureBindingOffset };
                    _workerQueue.push(task);
                }

                for(auto lightId = 0u; lightId < pending->numLights; ++lightId) {
                    _workerQueue.push(LightUploadTask{ pending, lightId});
                }

                std::vector<uint32_t> lightInstances{};
                for(auto i = 0u; i < pending->gltf->nodes.size(); ++i) {
                    const auto& node = pending->gltf->nodes[i];
                    if(node.extensions.contains(KHR_lights_punctual)){
                        lightInstances.push_back(i);
                    }
                }

                for(auto instanceId = 0u; instanceId < lightInstances.size(); ++instanceId) {
                    const auto nodeId = lightInstances[instanceId];
                    auto node = pending->gltf->nodes[nodeId];
                    _workerQueue.push(LightInstanceUploadTask{ pending, node, nodeId, instanceId});
                }

                _taskPending.notifyAll();
            }

            while(!_pendingTextureUploads.empty()) {
                _workerQueue.push(_pendingTextureUploads.pop());
                _taskPending.notifyAll();
            }
        }
        spdlog::info("GLTF async loader offline");
    }

    void Loader::execute(const std::vector<VkCommandBuffer>& commandBuffers) {
        auto commandBuffer = _device->transferCommandPool().allocate(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        _fence.reset();

        auto beginInfo = initializers::commandBufferBeginInfo();
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        vkCmdExecuteCommands(commandBuffer, COUNT(commandBuffers), commandBuffers.data());

        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        vkQueueSubmit(_device->queues.transfer, 1, &submitInfo, _fence.fence);

        _fence.wait();
    }

    void Loader::workerLoop(int id) {
        auto workQueue = _workerQueue.buffer(id);
        bool running = true;

        std::vector<SecondaryCommandBuffer> batch{};
        while(running) {
            _taskPending.wait( [&]{ return !workQueue->empty(); } );

            batch.clear();
            auto availableTasks = workQueue->size();
            spdlog::info("available work: {}", availableTasks);
            while(availableTasks > 0) {
                auto task = workQueue->pop();
                --availableTasks;
                if (std::get_if<StopWorkerTask>(&task)) {
                    running = false;
                    break;
                }

                VkCommandBuffer commandBuffer{};
                try {
                    commandBuffer = processTask(task, id);
                    batch.push_back({{commandBuffer}, task});
                }catch(const std::exception& exception) {
                    handleError(task, exception);
                    continue;
                }

                if(batch.size() >= _commandBufferBatchSize) {
                    _commandBufferQueue.push(batch);
                    _coordinatorWorkAvailable.notify();
                    batch.clear();
                }
                spdlog::info("available work left: {}", availableTasks);
            }
            if(!batch.empty()){
                _commandBufferQueue.push(batch);
                _coordinatorWorkAvailable.notify();
            }
        }
        spdlog::info("GLTF worker[{}] going offline", id);
    }

    VkCommandBuffer Loader::processTask(Task& task, int workerId) {
        auto commandBuffer = _workerCommandPools[workerId].transferPool.allocate(VK_COMMAND_BUFFER_LEVEL_SECONDARY);

        auto beginInfo = initializers::commandBufferBeginInfo();
        static VkCommandBufferInheritanceInfo inheritanceInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO };
        beginInfo.pInheritanceInfo = &inheritanceInfo;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        process(commandBuffer, std::get_if<MeshUploadTask>(&task), workerId);
        process(commandBuffer, std::get_if<GltfTextureUploadTask>(&task), workerId);
        process(commandBuffer, std::get_if<MaterialUploadTask>(&task), workerId);
        process(commandBuffer, std::get_if<InstanceUploadTask>(&task), workerId);
        process(commandBuffer, std::get_if<TextureUploadTask>(&task), workerId);
        process(commandBuffer, std::get_if<LightUploadTask>(&task), workerId);
        process(commandBuffer, std::get_if<LightInstanceUploadTask>(&task), workerId);

        vkEndCommandBuffer(commandBuffer);


        return commandBuffer;
    }

    void Loader::process(VkCommandBuffer commandBuffer, MeshUploadTask* meshUpload, int workerId) {
        if(!meshUpload) return;
//        spdlog::info("worker{} processing mesh upload", workerId);

        auto pending = meshUpload->pending;
        auto model = pending->model;

        auto vertexCount = getNumVertices(*pending->gltf, meshUpload->mesh);
        auto [u32, u16, u8] = getNumIndices(*pending->gltf, meshUpload->mesh);

        uint32_t vertexOffset = pending->offsets.vertex[meshUpload->meshId];
        uint32_t firstIndex8 = pending->offsets.u8[meshUpload->meshId];
        uint32_t firstIndex16 = pending->offsets.u16[meshUpload->meshId];
        uint32_t firstIndex32 = pending->offsets.u32[meshUpload->meshId];

//        spdlog::info("worker{}, {}, {}, {}", workerId, vertexOffset, firstIndex16, firstIndex32);

        std::array<VkBufferCopy, 4> regions{};
        regions[0].dstOffset = vertexOffset * sizeof(VertexMultiAttributes);
        regions[1].dstOffset = firstIndex8 * sizeof(uint8_t);
        regions[2].dstOffset = firstIndex16 * sizeof(uint16_t);
        regions[3].dstOffset = firstIndex32 * sizeof(uint32_t);

        
        for(const auto& primitive : meshUpload->mesh.primitives) {
            auto indexType = to<ComponentType>(pending->gltf->accessors[primitive.indices].componentType);
            const auto numVertices = getNumVertices(*pending->gltf, primitive);
            std::vector<VertexMultiAttributes> vertices;
            vertices.reserve(numVertices);

            VkDeviceSize indicesByteSize = 0;

            if(to<ComponentType>(pending->gltf->accessors[primitive.indices].componentType) == ComponentType::UNSIGNED_BYTE) {
                indicesByteSize = pending->gltf->accessors[primitive.indices].count * sizeof(uint8_t);
            }else if(to<ComponentType>(pending->gltf->accessors[primitive.indices].componentType) == ComponentType::UNSIGNED_SHORT) {
                indicesByteSize = pending->gltf->accessors[primitive.indices].count * sizeof(uint16_t);
            }else {
                indicesByteSize = pending->gltf->accessors[primitive.indices].count * sizeof(uint32_t);
            }

            auto stagingBuffer = _stagingBuffers[workerId].allocate(numVertices * sizeof(VertexMultiAttributes) + indicesByteSize);
//            spdlog::info("buffer offset: {} size: {}", stagingBuffer.offset, stagingBuffer.size());

            auto positions = getAttributeData<glm::vec3>(*pending->gltf, primitive, "POSITION");
            auto normals = getAttributeData<glm::vec3>(*pending->gltf, primitive, "NORMAL");
            auto tangents = getAttributeData<glm::vec4>(*pending->gltf, primitive, "TANGENT");

            for(auto i = 0; i < numVertices; ++i) {
                VertexMultiAttributes vertex{};
                vertex.position = glm::vec4(positions[i], 1);
                vertex.normal = normals.empty() ? glm::vec3(0, 0, 1) : normals[i];
                vertex.tangent = tangents.empty() ? glm::vec3(0) : tangents[i].xyz();
                vertex.bitangent = tangents.empty() ? glm::vec3(0) : glm::cross(normals[i], tangents[i].xyz()) * tangents[i].w;
                vertices.push_back(vertex);
            }

            for(auto j = 0; j < 2; ++j) {
                auto colors3 = getAttributeData<glm::vec3>(*pending->gltf, primitive, fmt::format("COLOR_{}", j));
                auto colors4 = getAttributeData<glm::vec4>(*pending->gltf, primitive, fmt::format("COLOR_{}", j));
                auto joints = getAttributeData<glm::vec4>(*pending->gltf, primitive, fmt::format("JOINTS_{}", j));
                auto weights = getAttributeData<glm::vec4>(*pending->gltf, primitive, fmt::format("WEIGHTS_{}", j));
                auto uvs = getAttributeData<glm::vec2>(*pending->gltf, primitive, fmt::format("TEXCOORD_{}", j));

                if(!colors3.empty() || !colors4.empty() || !uvs.empty() || !joints.empty() || !weights.empty())
                for(auto i = 0; i  < numVertices; ++i) {
                    vertices[i].color[j] = !colors3.empty() ? glm::vec4(colors3[i], 1) : (!colors4.empty() ? colors4[i] : glm::vec4(1));
//                    vertices[i].joint = !joints.empty() ? glm::vec4(joints[i]) : glm::vec4(0);
//                    vertices[i].weight = !weights.empty() ? glm::vec4(weights[i]) : glm::vec4(0);
                    vertices[i].uv[j] = uvs.empty() ? glm::vec2(0) : uvs[i];
                }
            }

            regions[0].srcOffset = stagingBuffer.offset;
            regions[0].size = BYTE_SIZE(vertices);

            if(indexType == ComponentType::UNSIGNED_BYTE) {
                auto pIndices = getIndices<uint8_t>(*pending->gltf, primitive);
                std::vector<uint8_t> indices{pIndices.begin(), pIndices.end()};
                regions[1].srcOffset = regions[0].srcOffset + regions[0].size;
                regions[1].size = BYTE_SIZE(indices);
                stagingBuffer.copy(indices.data(), regions[1].size, regions[0].size);

                if(tangents.empty()) {
                    calculateTangents(vertices, indices);
                }
            }else if(indexType == ComponentType::UNSIGNED_SHORT) {
                auto pIndices = getIndices<uint16_t>(*pending->gltf, primitive);
                std::vector<uint16_t> indices{pIndices.begin(), pIndices.end()};
                regions[2].srcOffset = regions[0].srcOffset + regions[0].size;
                regions[2].size = BYTE_SIZE(indices);
                stagingBuffer.copy(indices.data(), regions[2].size, regions[0].size);

                if(tangents.empty()) {
                    calculateTangents(vertices, indices);
                }
            }else {
                auto pIndices = getIndices<uint32_t>(*pending->gltf, primitive);
                std::vector<uint32_t> indices{pIndices.begin(), pIndices.end()};
                regions[3].srcOffset = regions[0].srcOffset + regions[0].size;
                regions[3].size = BYTE_SIZE(indices);
                stagingBuffer.copy(indices.data(), regions[3].size, regions[0].size);

                if(tangents.empty()) {
                    calculateTangents(vertices, indices);
                }
            }

            stagingBuffer.copy(vertices.data(), regions[0].size,  0);


            assert(model->vertices.size >= regions[0].dstOffset);
            vkCmdCopyBuffer(commandBuffer, *stagingBuffer.buffer, model->vertices, 1, &regions[0]);

            if(indexType == ComponentType::UNSIGNED_BYTE) {
                assert(model->indices.u8.handle.size >= regions[1].dstOffset);
                vkCmdCopyBuffer(commandBuffer, *stagingBuffer.buffer, model->indices.u8.handle, 1, &regions[1]);
            }else  if(indexType == ComponentType::UNSIGNED_SHORT) {
                assert(model->indices.u16.handle.size >= regions[2].dstOffset);
                vkCmdCopyBuffer(commandBuffer, *stagingBuffer.buffer, model->indices.u16.handle, 1, &regions[2]);
            }else {
                assert(model->indices.u32.handle.size >= regions[3].dstOffset);
                vkCmdCopyBuffer(commandBuffer, *stagingBuffer.buffer, model->indices.u32.handle, 1, &regions[3]);
            }

            auto& vertexBarrier = *_barrierObjectPools[workerId].acquire();
            vertexBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            vertexBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vertexBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            vertexBarrier.srcQueueFamilyIndex = _device->queueFamilyIndex.transfer.value();
            vertexBarrier.dstQueueFamilyIndex = _device->queueFamilyIndex.graphics.value();
            vertexBarrier.offset = regions[0].dstOffset;
            vertexBarrier.buffer = model->vertices;
            vertexBarrier.size = regions[0].size;

            auto& indexBarrier = *_barrierObjectPools[workerId].acquire();

            indexBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            indexBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            indexBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            indexBarrier.srcQueueFamilyIndex = _device->queueFamilyIndex.transfer.value();
            indexBarrier.dstQueueFamilyIndex = _device->queueFamilyIndex.graphics.value();

            if(indexType == ComponentType::UNSIGNED_BYTE) {
                indexBarrier.offset = regions[1].dstOffset;
                indexBarrier.buffer = model->indices.u8.handle;
                indexBarrier.size = regions[1].size;
            }else if(indexType == ComponentType::UNSIGNED_SHORT) {
                indexBarrier.offset = regions[2].dstOffset;
                indexBarrier.buffer = model->indices.u16.handle;
                indexBarrier.size = regions[2].size;
            }else {
                indexBarrier.offset = regions[3].dstOffset;
                indexBarrier.buffer = model->indices.u32.handle;
                indexBarrier.size = regions[3].size;
            }

            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0,
                                 VK_NULL_HANDLE, 1, &vertexBarrier, 0, VK_NULL_HANDLE);

            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0,
                                 VK_NULL_HANDLE, 1, &indexBarrier, 0, VK_NULL_HANDLE);


            Mesh mesh{
                    .meshId = meshUpload->meshId,
                    .indexCount = static_cast<uint32_t>(pending->gltf->accessors[primitive.indices].count),
                    .instanceCount = 1,
                    .firstIndex =  indexType == ComponentType::UNSIGNED_SHORT ? firstIndex16 : firstIndex32,
                    .vertexOffset = vertexOffset,
                    .firstInstance = 0,
                    .materialId = primitive.material == -1 ? 0 : static_cast<uint32_t>(primitive.material),
                    .indexType = indexType,
                    .name = meshUpload->mesh.name
            };
            meshUpload->primitives.push_back(mesh);

            if(indexType == ComponentType::UNSIGNED_BYTE) {
                firstIndex8 += mesh.indexCount;
            }else if(indexType == ComponentType::UNSIGNED_SHORT) {
                firstIndex16 += mesh.indexCount;
            }else if(indexType == ComponentType::UNSIGNED_INT){
                firstIndex32 += mesh.indexCount;
            }
            vertexOffset += vertices.size();
            regions[0].dstOffset += regions[0].size;

            if(indexType == ComponentType::UNSIGNED_BYTE) {
                regions[1].dstOffset += regions[1].size;
            }else if(indexType == ComponentType::UNSIGNED_SHORT) {
                regions[2].dstOffset += regions[2].size;
            }else {
                regions[3].dstOffset += regions[3].size;
            }
        }
    }

    void Loader::process(VkCommandBuffer commandBuffer, GltfTextureUploadTask* textureUpload, int workerId) {
        if(!textureUpload) return;

        VkImageCreateInfo createInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        createInfo.imageType = VK_IMAGE_TYPE_2D;
        createInfo.format = VK_FORMAT_R8G8B8A8_UNORM; // TODO derive format i.e. getFormat(imate);
        createInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.mipLevels = 1; // TODO generate mip levels;
        createInfo.arrayLayers = 1;
        createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        auto& prepTransferBarrier = *_imageMemoryBarrierObjectPools[workerId].acquire();
        auto& prepReadBarrier = *_imageMemoryBarrierObjectPools[workerId].acquire();

        auto& region = *_bufferImageCopyPool[workerId].acquire();

        auto textureId = textureUpload->textureId;
        auto& texture = textureUpload->pending->textures[textureId];
        const auto& image = textureUpload->pending->gltf->images[textureUpload->texture.source];
        texture.width = static_cast<uint32_t>(image.width);
        texture.height = static_cast<uint32_t>(image.height);
        texture.levels = static_cast<uint32_t>(std::log2(std::max(texture.width, texture.height))) + 1;
        createInfo.mipLevels = texture.levels;

        createInfo.extent = { texture.width, texture.height, 1 };

        texture.image = _device->createImage(createInfo);
        _device->setName<VK_OBJECT_TYPE_IMAGE>(image.uri, texture.image.image);

        VkImageSubresourceRange subresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, texture.levels, 0, 1};
        texture.imageView = texture.image.createView(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);

        texture.sampler = createSampler(*textureUpload->pending->gltf, textureUpload->texture.sampler, texture.levels);

        constexpr VkDeviceSize alignment = 4; // buffer offset should be multiple of VK_FORMAT_R8G8B8A8_UNORM (4 bytes)
        auto stagingBuffer = _stagingBuffers[workerId].allocate(image.image.size(), alignment);
        stagingBuffer.upload(image.image.data());

        prepTransferBarrier.image = texture.image.image;
        prepTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        prepTransferBarrier.subresourceRange = subresourceRange;
        prepTransferBarrier.srcAccessMask = VK_ACCESS_NONE;
        prepTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        prepTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        prepTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &prepTransferBarrier);

        region.bufferOffset = stagingBuffer.offset;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = { texture.width, texture.height, 1 };
        vkCmdCopyBufferToImage(commandBuffer, *stagingBuffer.buffer, texture.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        prepReadBarrier.image = texture.image.image;
        prepReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        prepReadBarrier.subresourceRange = subresourceRange;
        prepReadBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        prepReadBarrier.dstAccessMask = VK_ACCESS_NONE;
        prepReadBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        prepReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        prepReadBarrier.srcQueueFamilyIndex = *_device->queueFamilyIndex.transfer;
        prepReadBarrier.dstQueueFamilyIndex = *_device->queueFamilyIndex.graphics;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_NONE, 0, 0, nullptr, 0, nullptr, 1, &prepReadBarrier);

        textureUpload->textureId = textureId;
    }

    void Loader::process(VkCommandBuffer commandBuffer, MaterialUploadTask* materialUpload, int workerId) {
        if(!materialUpload) return;

        MaterialData material{};
        material.baseColor = vec4From(materialUpload->material.pbrMetallicRoughness.baseColorFactor);
        material.roughness = static_cast<float>(materialUpload->material.pbrMetallicRoughness.roughnessFactor);
        material.metalness = static_cast<float>(materialUpload->material.pbrMetallicRoughness.metallicFactor);
        material.emission = vec3From(materialUpload->material.emissiveFactor);
        material.alphaMode = alphaModeToIndex(materialUpload->material.alphaMode);
        material.alphaCutOff = static_cast<float>(materialUpload->material.alphaCutoff);
        material.doubleSided = materialUpload->material.doubleSided;
        material.textureInfoOffset = materialUpload->materialId;
        
        auto& textureInfos = materialUpload->textureInfos;

        const auto offset = materialUpload->pending->textureBindingOffset;
        textureInfos[static_cast<int>(TextureType::BASE_COLOR)] = extract(materialUpload->material.pbrMetallicRoughness.baseColorTexture,  offset);
        textureInfos[static_cast<int>(TextureType::NORMAL)] = extract(materialUpload->material.normalTexture, offset);
        textureInfos[static_cast<int>(TextureType::METALLIC_ROUGHNESS)] = extract(materialUpload->material.pbrMetallicRoughness.metallicRoughnessTexture, offset);
        textureInfos[static_cast<int>(TextureType::EMISSION)] = extract(materialUpload->material.emissiveTexture, offset);
        textureInfos[static_cast<int>(TextureType::OCCLUSION)] = extract(materialUpload->material.occlusionTexture, offset);

        if(materialUpload->material.extensions.contains(KHR_materials_transmission)){
            material.transmission = materialUpload->material.extensions.at(KHR_materials_transmission).Get("transmissionFactor").GetNumberAsDouble();
        }

        if(materialUpload->material.extensions.contains(KHR_materials_emissive_strength)){
            material.emissiveStrength = materialUpload->material.extensions.at(KHR_materials_emissive_strength).Get("emissiveStrength").GetNumberAsDouble();
        }

        if(materialUpload->material.extensions.contains(KHR_materials_dispersion)){
            material.dispersion = materialUpload->material.extensions.at(KHR_materials_dispersion).Get("dispersion").GetNumberAsDouble();
        }

        if(materialUpload->material.extensions.contains(KHR_materials_ior)){
            material.ior = materialUpload->material.extensions.at(KHR_materials_ior).Get("ior").GetNumberAsDouble();
        }

        if(materialUpload->material.extensions.contains(KHR_materials_volume)) {

            if(materialUpload->material.extensions.at(KHR_materials_volume).Has("thicknessFactor")) {
                material.thickness = materialUpload->material.extensions.at(KHR_materials_volume).Get( "thicknessFactor").GetNumberAsDouble();
            }

            if(materialUpload->material.extensions.at(KHR_materials_volume).Has("attenuationDistance")) {
                material.attenuationDistance = materialUpload->material.extensions.at(KHR_materials_volume).Get("attenuationDistance").GetNumberAsDouble();
            }

            if(materialUpload->material.extensions.at(KHR_materials_volume).Has("attenuationColor")) {
                material.attenuationColor.r = materialUpload->material.extensions.at(KHR_materials_volume).Get("attenuationColor").Get(0).GetNumberAsDouble();
                material.attenuationColor.g = materialUpload->material.extensions.at(KHR_materials_volume).Get( "attenuationColor").Get(1).GetNumberAsDouble();
                material.attenuationColor.b = materialUpload->material.extensions.at(KHR_materials_volume).Get( "attenuationColor").Get(2).GetNumberAsDouble();
            }

            if(materialUpload->material.extensions.at(KHR_materials_volume).Has("thicknessTexture")){
                auto textureIndex = materialUpload->material.extensions.at(KHR_materials_volume).Get("thicknessTexture").Get("index").GetNumberAsInt();
                if(textureIndex != -1) {
                    textureInfos[static_cast<int>(TextureType::THICKNESS)].index = textureIndex + materialUpload->pending->textureBindingOffset;
                }
            }
        }

        if(materialUpload->material.extensions.contains(KHR_materials_clearcoat)){
            const auto& clearCoat = materialUpload->material.extensions.at(KHR_materials_clearcoat);

            if(clearCoat.Has("clearcoatFactor")){
                material.clearCoatFactor = clearCoat.Get("clearcoatFactor").GetNumberAsDouble();
            }

            if(clearCoat.Has("clearcoatRoughnessFactor")){
                material.clearCoatRoughnessFactor = clearCoat.Get("clearcoatRoughnessFactor").GetNumberAsDouble();
            }

            if(clearCoat.Has("clearcoatTexture")){
                auto textureIndex = clearCoat.Get("clearcoatTexture").Get("index").GetNumberAsInt();
                textureInfos[static_cast<int>(TextureType::CLEAR_COAT_COLOR)].index = textureIndex + materialUpload->pending->textureBindingOffset;
            }

            if(clearCoat.Has("clearcoatRoughnessTexture")){
                auto textureIndex = clearCoat.Get("clearcoatRoughnessTexture").Get("index").GetNumberAsInt();
                textureInfos[static_cast<int>(TextureType::CLEAR_COAT_ROUGHNESS)].index = textureIndex + materialUpload->pending->textureBindingOffset;
            }

            if(clearCoat.Has("clearcoatNormalTexture")){
                auto textureIndex = clearCoat.Get("clearcoatNormalTexture").Get("index").GetNumberAsInt();
                textureInfos[static_cast<int>(TextureType::CLEAR_COAT_NORMAL)].index = textureIndex + materialUpload->pending->textureBindingOffset;
            }
        }

        extractSheen(material, *materialUpload);

        if(materialUpload->material.extensions.contains(KHR_materials_unlit)){
            material.unlit = 1;
        }

        auto stagingBuffer =  _stagingBuffers[workerId].allocate(sizeof(material));
        stagingBuffer.upload(&material);

        auto& region = *_bufferCopyPool[workerId].acquire();
        region.srcOffset = stagingBuffer.offset;
        region.dstOffset = materialUpload->materialId * sizeof(MaterialData);
        region.size = stagingBuffer.size();

        const auto model = materialUpload->pending->model;
        vkCmdCopyBuffer(commandBuffer, *stagingBuffer.buffer, model->materials, 1, &region);

        auto tiStagingBuffer = _stagingBuffers[workerId].allocate(BYTE_SIZE(textureInfos));
        tiStagingBuffer.upload(textureInfos.data());

        auto& tiRegion = *_bufferCopyPool[workerId].acquire();
        tiRegion.srcOffset = tiStagingBuffer.offset;
        tiRegion.dstOffset = material.textureInfoOffset * sizeof(TextureInfo) * NUM_TEXTURE_MAPPING;
        tiRegion.size = tiStagingBuffer.size();
        vkCmdCopyBuffer(commandBuffer, *tiStagingBuffer.buffer, model->textureInfos, 1, &tiRegion);

        auto& barrier = *_barrierObjectPools[workerId].acquire();
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.srcQueueFamilyIndex = _device->queueFamilyIndex.transfer.value();
        barrier.dstQueueFamilyIndex = _device->queueFamilyIndex.graphics.value();
        barrier.offset = region.dstOffset;
        barrier.buffer = model->materials;
        barrier.size = region.size;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0,
                             VK_NULL_HANDLE, 1, &barrier, 0, VK_NULL_HANDLE);

        auto& tiBarrier = *_barrierObjectPools[workerId].acquire();
        tiBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        tiBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        tiBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        tiBarrier.srcQueueFamilyIndex = _device->queueFamilyIndex.transfer.value();
        tiBarrier.dstQueueFamilyIndex = _device->queueFamilyIndex.graphics.value();
        tiBarrier.offset = tiRegion.dstOffset;
        tiBarrier.buffer = model->textureInfos;
        tiBarrier.size = tiRegion.size;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0,
                             VK_NULL_HANDLE, 1, &tiBarrier, 0, VK_NULL_HANDLE);
    }

    void Loader::process(VkCommandBuffer commandBuffer, InstanceUploadTask* instanceUpload, int workerId) {
        if(!instanceUpload) return;


//        spdlog::info("worker{} processing Instance upload", workerId);

        std::map<uint32_t, std::vector<Mesh>> groups;

        for(auto& mesh : instanceUpload->primitives) {
            if(!groups.contains(mesh.meshId)){
                groups[mesh.meshId] = {};
            }
            groups[mesh.meshId].push_back(mesh);
        }

        std::map<uint32_t, std::vector<uint32_t>> meshInstances{};
        const auto& gltf_model = instanceUpload->pending->gltf;
        for(auto nodeId = 0; nodeId < gltf_model->nodes.size(); ++nodeId) {
            auto meshId = gltf_model->nodes[nodeId].mesh;
            if(groups.contains(meshId)){
                if(!meshInstances.contains(meshId)){
                    meshInstances[meshId] = {};
                }
                meshInstances[meshId].push_back(nodeId);
            }

        }

        struct {
            std::vector<VkDrawIndexedIndirectCommand> u8;
            std::vector<VkDrawIndexedIndirectCommand> u16;
            std::vector<VkDrawIndexedIndirectCommand> u32;
        } drawCommands;

        struct {
            std::vector<MeshData> u8;
            std::vector<MeshData> u16;
            std::vector<MeshData> u32;
        } meshData;

        auto transforms = instanceUpload->pending->transforms;

        for(const auto& [meshId, instanceIds] : meshInstances) {
            const auto meshes = groups[meshId];

            for(auto instanceId : instanceIds) {
                for (auto mesh: meshes) {
                    VkDrawIndexedIndirectCommand drawCommand{};
                    drawCommand.indexCount = mesh.indexCount;
                    drawCommand.instanceCount = 1;
                    drawCommand.firstIndex = mesh.firstIndex;
                    drawCommand.vertexOffset = mesh.vertexOffset;
                    drawCommand.firstInstance = mesh.firstInstance;

                    MeshData data{.materialId = static_cast<int>(mesh.materialId)};
                    data.model = transforms[instanceId];
                    data.ModelInverse = glm::inverse(data.model);

                    if(mesh.indexType == ComponentType::UNSIGNED_BYTE) {
                        drawCommands.u8.push_back(drawCommand);
                        meshData.u8.push_back(data);
                    }else if(mesh.indexType == ComponentType::UNSIGNED_SHORT) {
                        drawCommands.u16.push_back(drawCommand);
                        meshData.u16.push_back(data);
                    }else {
                        drawCommands.u32.push_back(drawCommand);
                        meshData.u32.push_back(data);
                    }
                }
            }
        }

        struct {
            uint32_t u8{};
            uint32_t u16{};
            uint32_t u32{};
        } drawOffset;

        drawOffset.u8 = instanceUpload->pending->drawOffset.u8.fetch_add(drawCommands.u8.size());
        drawOffset.u16 = instanceUpload->pending->drawOffset.u16.fetch_add(drawCommands.u16.size());
        drawOffset.u32 = instanceUpload->pending->drawOffset.u32.fetch_add(drawCommands.u32.size());

        if(!drawCommands.u16.empty()) {
//            spdlog::info("offset: {}, meshInstances: {}", drawOffset.u16, meshInstances);
        }


        Buffer drawCmdStaging{};
        drawCmdStaging.u8.rHandle =  _stagingBuffers[workerId].allocate(BYTE_SIZE(drawCommands.u8) + sizeof(VkDrawIndexedIndirectCommand));
        drawCmdStaging.u16.rHandle =  _stagingBuffers[workerId].allocate(BYTE_SIZE(drawCommands.u16) + sizeof(VkDrawIndexedIndirectCommand));
        drawCmdStaging.u32.rHandle = _stagingBuffers[workerId].allocate(BYTE_SIZE(drawCommands.u32) + sizeof(VkDrawIndexedIndirectCommand));

        Buffer meshStaging{};
        meshStaging.u8.rHandle = _stagingBuffers[workerId].allocate(BYTE_SIZE(meshData.u8) + sizeof(MeshData));
        meshStaging.u16.rHandle = _stagingBuffers[workerId].allocate(BYTE_SIZE(meshData.u16) + sizeof(MeshData));
        meshStaging.u32.rHandle = _stagingBuffers[workerId].allocate(BYTE_SIZE(meshData.u32) + sizeof(MeshData));


        const auto model = instanceUpload->pending->model;
        if(!drawCommands.u8.empty()) {
            meshStaging.u8.rHandle.upload(meshData.u8.data());
            drawCmdStaging.u8.rHandle.upload(drawCommands.u8.data());
            transferMeshInstance(commandBuffer, drawCmdStaging.u8.rHandle, model->draw.u8.handle, meshStaging.u8.rHandle, model->meshes.u8.handle, drawOffset.u8, workerId);
        }

        if(!drawCommands.u16.empty()) {
            meshStaging.u16.rHandle.upload(meshData.u16.data());
            drawCmdStaging.u16.rHandle.upload(drawCommands.u16.data());
            transferMeshInstance(commandBuffer, drawCmdStaging.u16.rHandle, model->draw.u16.handle, meshStaging.u16.rHandle, model->meshes.u16.handle, drawOffset.u16, workerId);
        }

        if(!drawCommands.u32.empty()) {
            meshStaging.u32.rHandle.upload(meshData.u32.data());
            drawCmdStaging.u32.rHandle.upload(drawCommands.u32.data());
            transferMeshInstance(commandBuffer, drawCmdStaging.u32.rHandle, model->draw.u32.handle, meshStaging.u32.rHandle, model->meshes.u32.handle, drawOffset.u32, workerId);

        }

        instanceUpload->drawCounts.u8 = drawCommands.u8.size();
        instanceUpload->drawCounts.u16 = drawCommands.u16.size();
        instanceUpload->drawCounts.u32 = drawCommands.u32.size();
    }

    void Loader::transferMeshInstance(VkCommandBuffer commandBuffer, BufferRegion &drawCmdSrc, VulkanBuffer &drawCmdDst, BufferRegion &meshDataSrc, VulkanBuffer &meshDataDst, uint32_t drawOffset, int workerId) {
        VkDeviceSize meshOffset = drawOffset * sizeof(MeshData);
        VkDeviceSize drawCmdOffset = drawOffset * sizeof(VkDrawIndexedIndirectCommand);

        assert(drawCmdSrc.size() + drawCmdOffset <= drawCmdDst.size);
        assert(meshDataSrc.size() + meshOffset <= meshDataDst.size);

        VkBufferCopy& meshRegion = *_bufferCopyPool[workerId].acquire();
        meshRegion.srcOffset = meshDataSrc.offset;
        meshRegion.dstOffset = meshOffset;
        meshRegion.size = meshDataSrc.size() - sizeof(MeshData);


        vkCmdCopyBuffer(commandBuffer, *meshDataSrc.buffer, meshDataDst, 1, &meshRegion);

        VkBufferCopy& drawRegion = *_bufferCopyPool[workerId].acquire();
        drawRegion.srcOffset = drawCmdSrc.offset;
        drawRegion.dstOffset = drawCmdOffset;
        drawRegion.size = drawCmdSrc.size() - sizeof(VkDrawIndexedIndirectCommand);


        vkCmdCopyBuffer(commandBuffer, *drawCmdSrc.buffer, drawCmdDst, 1, &drawRegion);

        auto& meshBarrier = *_barrierObjectPools[workerId].acquire();
        meshBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        meshBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        meshBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        meshBarrier.srcQueueFamilyIndex = _device->queueFamilyIndex.transfer.value();
        meshBarrier.dstQueueFamilyIndex = _device->queueFamilyIndex.graphics.value();
        meshBarrier.offset = meshRegion.dstOffset;
        meshBarrier.buffer = meshDataDst;
        meshBarrier.size = meshRegion.size;

        auto& drawCmdBarrier = *_barrierObjectPools[workerId].acquire();
        drawCmdBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        drawCmdBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        drawCmdBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        drawCmdBarrier.srcQueueFamilyIndex = _device->queueFamilyIndex.transfer.value();
        drawCmdBarrier.dstQueueFamilyIndex = _device->queueFamilyIndex.graphics.value();
        drawCmdBarrier.offset = drawRegion.dstOffset;
        drawCmdBarrier.buffer = drawCmdDst;
        drawCmdBarrier.size = drawRegion.size;



        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, VK_NULL_HANDLE, 1, &meshBarrier, 0, VK_NULL_HANDLE);
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, VK_NULL_HANDLE, 1, &drawCmdBarrier, 0, VK_NULL_HANDLE);
    }

    void Loader::process(VkCommandBuffer commandBuffer, TextureUploadTask* textureUpload, int workerId) {
        if(!textureUpload) return;

        auto& texture = *textureUpload->status->texture;

        int width, height, channel;
        std::variant<stbi_uc*, float*> pixels;

        stbi_set_flip_vertically_on_load(int(texture.flipped));
        if(isFloat(texture.format)){
            pixels = stbi_loadf(textureUpload->path.data(), &width, &height, &channel, textureUpload->desiredChannels);
        }else {
            pixels = stbi_load(textureUpload->path.data(), &width, &height, &channel, textureUpload->desiredChannels);
        }


        std::visit([&](auto data){
            if(!data){
                throw std::runtime_error(fmt::format("failed to load texture image {}!", textureUpload->path));
            }
        }, pixels);


        VkDeviceSize alignment = textures::byteSize(texture.format) * textureUpload->desiredChannels;
        auto stagingBuffer = _stagingBuffers[workerId].allocate(texture.image.size, alignment);
        std::visit([&](auto data){ stagingBuffer.upload(data); }, pixels);

        auto& prepTransferBarrier = *_imageMemoryBarrierObjectPools[workerId].acquire();
        auto& prepReadBarrier = *_imageMemoryBarrierObjectPools[workerId].acquire();

        auto& region = *_bufferImageCopyPool[workerId].acquire();

        VkImageSubresourceRange subresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, texture.levels, 0, 1};
        prepTransferBarrier.image = texture.image.image;
        prepTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        prepTransferBarrier.subresourceRange = subresourceRange;
        prepTransferBarrier.srcAccessMask = VK_ACCESS_NONE;
        prepTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        prepTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        prepTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &prepTransferBarrier);

        region.bufferOffset = stagingBuffer.offset;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = { texture.width, texture.height, 1 };
        vkCmdCopyBufferToImage(commandBuffer, *stagingBuffer.buffer, texture.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        prepReadBarrier.image = texture.image.image;
        prepReadBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        prepReadBarrier.subresourceRange = subresourceRange;
        prepReadBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        prepReadBarrier.dstAccessMask = VK_ACCESS_NONE;
        prepReadBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        prepReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        prepReadBarrier.srcQueueFamilyIndex = *_device->queueFamilyIndex.transfer;
        prepReadBarrier.dstQueueFamilyIndex = *_device->queueFamilyIndex.graphics;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_NONE, 0, 0, nullptr, 0, nullptr, 1, &prepReadBarrier);
    }

    void Loader::process(VkCommandBuffer commandBuffer, LightUploadTask *lightUpload, int workerId) {
        if(!lightUpload) return;

        const auto& lightExt = lightUpload->pending->gltf->extensions.at(KHR_lights_punctual).Get("lights").Get(lightUpload->LightId);
        Light light{};
        light.type = lightExt.Get("type").GetNumberAsInt();

        if(lightExt.Has("color")) {
            light.color = vec3From(lightExt.Get("color"));
        }

        if(lightExt.Has("intensity")) {
            light.intensity = lightExt.Get("intensity").GetNumberAsDouble();
        }

        if(lightExt.Has("range")) {
            light.range = lightExt.Get("range").GetNumberAsDouble();
        }

        if( to<LightType>(light.type) == LightType::SPOT ) {
            const auto& spot = lightExt.Get("spot");
            light.outerConeCos = spot.Has("outerConeAngle") ? spot.Get("outerConeAngle").GetNumberAsDouble() : glm::quarter_pi<double>();
            light.outerConeCos = spot.Has("outerConeAngle") ?  spot.Get("outerConeAngle").GetNumberAsDouble() : glm::quarter_pi<double>();
        }

        auto stagingBuffer =  _stagingBuffers[workerId].allocate(sizeof(light));
        stagingBuffer.upload(&light);

        auto& region = *_bufferCopyPool[workerId].acquire();
        region.srcOffset = stagingBuffer.offset;
        region.dstOffset = lightUpload->LightId * sizeof(Light);
        region.size = stagingBuffer.size();

        const auto model = lightUpload->pending->model;
        vkCmdCopyBuffer(commandBuffer, *stagingBuffer.buffer, model->lights, 1, &region);

        auto& barrier = *_barrierObjectPools[workerId].acquire();
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.srcQueueFamilyIndex = _device->queueFamilyIndex.transfer.value();
        barrier.dstQueueFamilyIndex = _device->queueFamilyIndex.graphics.value();
        barrier.offset = region.dstOffset;
        barrier.buffer = model->lights;
        barrier.size = region.size;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0,
                             VK_NULL_HANDLE, 1, &barrier, 0, VK_NULL_HANDLE);

    }


    void Loader::process(VkCommandBuffer commandBuffer, LightInstanceUploadTask *lightInstanceUpload, int workerId) {
        if(!lightInstanceUpload) return;

        LightInstance lightInstance{};
        lightInstance.lightId = lightInstanceUpload->node.extensions.at(KHR_lights_punctual).Get("light").GetNumberAsInt();

        lightInstance.model = lightInstanceUpload->pending->transforms[lightInstanceUpload->nodeId];
        lightInstance.ModelInverse = glm::inverse(lightInstance.model);

        auto stagingBuffer =  _stagingBuffers[workerId].allocate(sizeof(lightInstance));
        stagingBuffer.upload(&lightInstance);

        auto& region = *_bufferCopyPool[workerId].acquire();
        region.srcOffset = stagingBuffer.offset;
        region.dstOffset = lightInstanceUpload->instanceId * sizeof(LightInstance);
        region.size = stagingBuffer.size();

        const auto model = lightInstanceUpload->pending->model;
        vkCmdCopyBuffer(commandBuffer, *stagingBuffer.buffer, model->lightInstances, 1, &region);

        auto& barrier = *_barrierObjectPools[workerId].acquire();
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.srcQueueFamilyIndex = _device->queueFamilyIndex.transfer.value();
        barrier.dstQueueFamilyIndex = _device->queueFamilyIndex.graphics.value();
        barrier.offset = region.dstOffset;
        barrier.buffer = model->lightInstances;
        barrier.size = region.size;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0,
                             VK_NULL_HANDLE, 1, &barrier, 0, VK_NULL_HANDLE);
    }

    void Loader::stop() {
        _running = false;
        _coordinatorWorkAvailable.notify();
        _coordinator.join();
    }

    void Loader::initCommandPools() {
        _graphicsCommandPool = _device->createCommandPool(*_device->queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, 1);

        for(auto i = 0; i < _workerCount; ++i) {
            WorkerCommandPools commandPools{};
            // TODO check if transfer and compute queues are unique, if not use second queue, i.e. queue index 1
            commandPools.transferPool = std::move(_device->createCommandPool(*_device->queueFamilyIndex.transfer, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT));
            commandPools.graphicsPool = std::move(_device->createCommandPool(*_device->queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, 1));

            if(_device->queueFamilyIndex.compute.has_value()) {
                commandPools.computePool = std::move(_device->createCommandPool(*_device->queueFamilyIndex.compute,VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, 1));
            }else {
               spdlog::warn("Loader: no compute queue available");
            }
            _workerCommandPools.push_back(std::move(commandPools));
        }
    }

    void Loader::onComplete(Task& task) {
        onComplete(std::get_if<MeshUploadTask>(&task));
        onComplete(std::get_if<InstanceUploadTask>(&task));
        onComplete(std::get_if<GltfTextureUploadTask>(&task));
        onComplete(std::get_if<MaterialUploadTask>(&task));
        onComplete(std::get_if<TextureUploadTask>(&task));
        onComplete(std::get_if<LightUploadTask>(&task));
        onComplete(std::get_if<LightInstanceUploadTask>(&task));
    }

    void Loader::onComplete(MeshUploadTask *meshUpload) {
        if(!meshUpload) return;
        _workerQueue.push(InstanceUploadTask{ meshUpload->pending, meshUpload->primitives});
    }

    void Loader::onComplete(InstanceUploadTask *instanceUpload) {
        if(!instanceUpload) return;
        auto model = instanceUpload->pending->model;
        model->draw.u8.count += instanceUpload->drawCounts.u8;
        model->draw.u16.count += instanceUpload->drawCounts.u16;
        model->draw.u32.count += instanceUpload->drawCounts.u32;

        if((model->draw.u8.count + model->draw.u16.count + model->draw.u32.count) == model->numMeshes) {
            model->_loaded.notify();
        }

//        spdlog::info("i32: {}, 116: {}", instanceUpload->pending->model->draw.u32.count, instanceUpload->pending->model->draw.u16.count);
    }

    void Loader::onComplete(GltfTextureUploadTask *textureUpload) {
        if(!textureUpload) return;
        _readyTextures.push(*textureUpload);
        finalizeGltfTextureTransfer();
    }

    void Loader::onComplete(TextureUploadTask *textureUpload) {
        if(!textureUpload) return;
        _uploadedTextures.push(*textureUpload);
        finalizeRegularTextureTransfer();
    }

    void Loader::onComplete(MaterialUploadTask *materialUpload) {
        if(!materialUpload) return;
//        spdlog::info("material{} upload complete", materialUpload->materialId);
    }

    void Loader::commandBufferBatchSize(size_t size) {
        _commandBufferBatchSize = size;
    }

    VulkanDescriptorSetLayout Loader::descriptorSetLayout() const {
        return _descriptorSetLayout;
    }

    VulkanDescriptorSetLayout Loader::materialDescriptorSetLayout() const {
        return _materialDescriptorSetLayout;
    }


    void Loader::finalizeGltfTextureTransfer() {
        if(!_readyTextures.empty()) {
            std::vector<GltfTextureUploadTask> textureUploads;
            _graphicsCommandPool.oneTimeCommand([&](auto commandBuffer) {

                while (!_readyTextures.empty()) {
                    auto textureUpload = _readyTextures.pop();
                    auto &texture = textureUpload.pending->textures[textureUpload.textureId];

                    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                    barrier.image = texture.image.image;
                    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, texture.levels, 0, 1};
                    barrier.srcAccessMask = VK_ACCESS_NONE;
                    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    barrier.srcQueueFamilyIndex = *_device->queueFamilyIndex.transfer;
                    barrier.dstQueueFamilyIndex = *_device->queueFamilyIndex.graphics;
                    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                         0, 0, nullptr, 0, nullptr, 1, &barrier);

                    textures::generateLOD(commandBuffer, texture.image, texture.width, texture.height, texture.levels);
                    textureUploads.push_back(std::move(textureUpload));
                }
            });

            for (auto &textureUpload: textureUploads) {
                auto &texture = textureUpload.pending->textures[textureUpload.textureId];
                _bindlessDescriptor->update(
                        {&texture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureUpload.bindingId});
                textureUpload.pending->model->textures.push_back(std::move(texture));
            }
        }
    }

    void Loader::finalizeRegularTextureTransfer() {
        if(!_uploadedTextures.empty()) {
            std::vector<std::shared_ptr<TextureUploadStatus>> uploadedTextures;

            _graphicsCommandPool.oneTimeCommand([&](auto commandBuffer){
                while(!_uploadedTextures.empty()){
                    auto uploadedTexture = _uploadedTextures.pop();
                    auto texture = uploadedTexture.status->texture;

                    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                    barrier.image = texture->image.image;
                    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, texture->levels, 0, 1};
                    barrier.srcAccessMask = VK_ACCESS_NONE;
                    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    barrier.srcQueueFamilyIndex = *_device->queueFamilyIndex.transfer;
                    barrier.dstQueueFamilyIndex = *_device->queueFamilyIndex.graphics;
                    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                         0, 0, nullptr, 0, nullptr, 1, &barrier);

                    if(texture->lod) {
                        textures::generateLOD(commandBuffer, texture->image, texture->width, texture->height, texture->levels);
                    }
                    uploadedTextures.push_back(uploadedTexture.status);
                }
            });

            for(auto status : uploadedTextures) {
                auto texture = status->texture;
                if(texture->bindingId != std::numeric_limits<uint32_t>::max()) {
                    _bindlessDescriptor->update({texture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(texture->bindingId)});
                }
                status->success = true;
                status->_ready.notifyAll();
                spdlog::info("{} successfully uploaded and ready to use", texture->path);
            }
        }
    }


    void tinyGltfLoad(tinygltf::Model& model, const std::string& path) {
        std::string err;
        std::string warn;

        tinygltf::TinyGLTF loader;
        stbi_set_flip_vertically_on_load(0);
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

    Counts getCounts(const tinygltf::Model &model) {
        Counts counts{};

        for(const auto& mesh : model.meshes) {
            for(const auto& primitive : mesh.primitives) {
                ++counts.primitives;
                counts.vertices += model.accessors[primitive.attributes.at("POSITION")].count;
                if (model.accessors[primitive.indices].componentType == static_cast<int>(ComponentType::UNSIGNED_BYTE)) {
                    counts.indices.u8 += model.accessors[primitive.indices].count;
                }else if (model.accessors[primitive.indices].componentType == static_cast<int>(ComponentType::UNSIGNED_SHORT)) {
                    counts.indices.u16 += model.accessors[primitive.indices].count;
                } else if (model.accessors[primitive.indices].componentType == static_cast<int>(ComponentType::UNSIGNED_INT)) {
                    counts.indices.u32 += model.accessors[primitive.indices].count;
                }
            }
        }

        for(const auto& node : model.nodes) {
            if(node.mesh != -1){
                for(const auto& prim : model.meshes[node.mesh].primitives) {
                    if(model.accessors[prim.indices].componentType == static_cast<int>(ComponentType::UNSIGNED_BYTE)) {
                        counts.instances.u8++;
                    }else if(model.accessors[prim.indices].componentType == static_cast<int>(ComponentType::UNSIGNED_SHORT)) {
                        counts.instances.u16++;
                    }else if(model.accessors[prim.indices].componentType == static_cast<int>(ComponentType::UNSIGNED_INT)){
                        counts.instances.u32++;
                    }
                }
            }
            if(node.extensions.contains(KHR_lights_punctual)) {
                ++counts.numLightInstances;
            }
        }

        if(model.extensions.contains(KHR_lights_punctual)) {
            counts.numLights = model.extensions.at(KHR_lights_punctual).Get("lights").Size();
        }

        return counts;
    }

    std::vector<glm::mat4> computeTransforms(const tinygltf::Model &model) {
        auto numNodes = model.nodes.size();
        std::vector<glm::mat4> transforms(numNodes, glm::mat4{1});

        for(auto i = 0; i < numNodes; ++i) {
            transforms[i] = getTransformation(model.nodes[i]);
        }

        const auto& nodes = model.nodes;
        std::function<void(int, glm::mat4)> visit = [&](int id, glm::mat4 parentTransform) {
            transforms[id] = parentTransform * transforms[id];
            if(!nodes[id].children.empty()) {
                for(auto child : nodes[id].children) {
                    visit(child, transforms[id]);
                }
            }
        };
        
        for(auto node : model.scenes[0].nodes) {
            visit(node, glm::mat4{1});
        }

        return transforms;
    }

    glm::mat4 getTransformation(const tinygltf::Node &node) {
        glm::mat4 transform{glm::vec4{}, glm::vec4{}, glm::vec4{}, glm::vec4{}};
        if(!node.matrix.empty()) {
            const auto m = node.matrix;
            transform = glm::mat4{
                m[0],  m[1],  m[2],  m[3],
                m[4],  m[5],  m[6],  m[7],
                m[8],  m[9],  m[10], m[11],
                m[12], m[13], m[14], m[15],
            };
            return transform;
        }

        glm::vec3 translation{0};
        if(node.translation.size() == 3) {
            translation = vec3From(node.translation);
        }

        glm::vec3 scale{1};
        if(node.scale.size() == 3) {
            scale = vec3From(node.scale);
        }

        glm::quat rotation{1, 0, 0, 0};
        if(node.rotation.size() == 4) {
            rotation = quaternionFrom(node.rotation);
        }

        return glm::translate(glm::mat4{1}, translation) * glm::mat4_cast(rotation) * glm::scale(glm::mat4{1}, scale);    
    }

    glm::vec2 vec2From(const std::vector<double>& v) {
        glm::vec2 res{};

        if(v.size() >= 1) res.x = v[0];
        if(v.size() >= 2) res.y = v[1];

        return res;
    }

    glm::vec3 vec3From(const std::vector<double>& v) {
        glm::vec3 res{};

        if(v.size() >= 1) res.x = v[0];
        if(v.size() >= 2) res.y = v[1];
        if(v.size() >= 3) res.z = v[2];

        return res;
    }

    glm::vec3 vec3From(const tinygltf::Value& v) {
        assert(v.IsArray());
        return glm::vec3{ v.Get(0).GetNumberAsDouble(), v.Get(1).GetNumberAsDouble(), v.Get(2).GetNumberAsDouble() };
    }

    glm::vec2 vec2From(const tinygltf::Value& v) {
        assert(v.IsArray());
        return glm::vec2{ v.Get(0).GetNumberAsDouble(), v.Get(1).GetNumberAsDouble() };
    }

    glm::vec4 vec4From(const std::vector<double>& v) {
        glm::vec4 res{};

        if(v.size() >= 1) res.x = v[0];
        if(v.size() >= 2) res.y = v[1];
        if(v.size() >= 3) res.z = v[2];
        if(v.size() >= 4) res.w = v[3];

        return res;
    }

    glm::quat quaternionFrom(const std::vector<double> &q) {
        glm::quat res{};
        if(q.empty()) return res;

        res.x = q[0];
        res.y = q[1];
        res.z = q[2];
        res.w = q[3];
    }

    std::tuple<glm::vec3, glm::vec3>
    computeBounds(const tinygltf::Model &model, const std::vector<glm::mat4> &transforms) {
        std::vector<std::tuple<glm::vec3, glm::vec3>> meshBounds;

        for(const auto& mesh : model.meshes) {
            auto bounds = computeBounds(model, mesh);
            meshBounds.push_back(bounds);
        }

        glm::vec3 min{MAX_FLOAT}, max{MIN_FLOAT};

        for(auto i = 0; i < model.nodes.size(); ++i) {
            const auto& node = model.nodes[i];
            if(node.mesh != -1) {
                auto [bmin, bmax] = meshBounds[node.mesh];
                bmin = (transforms[i] * glm::vec4(bmin, 1)).xyz();
                bmax = (transforms[i] * glm::vec4(bmax, 1)).xyz();

                auto temp = bmin;
                bmin = glm::min(bmin, bmax);
                bmax = glm::max(temp, bmax);

                min = glm::min(min, bmin);
                max = glm::max(max, bmax);
            }
        }

        return std::make_tuple(min, max);
    }

    std::tuple<glm::vec3, glm::vec3> computeBounds(const tinygltf::Model &model, const tinygltf::Mesh &mesh) {

        glm::vec3 min{MAX_FLOAT}, max{MIN_FLOAT};

        for(const auto& primitive : mesh.primitives) {
            auto bmin = vec3From(model.accessors[primitive.attributes.at("POSITION")].minValues);
            auto bmax = vec3From(model.accessors[primitive.attributes.at("POSITION")].maxValues);

            min = glm::min(min, bmin);
            max = glm::max(max, bmax);
        }

        return std::make_tuple(min, max);
    }

    std::vector<glm::mat4> computePlaceHolders(const tinygltf::Model &model, const std::vector<glm::mat4>& transforms) {
        std::vector<glm::mat4> meshTransforms;

        for(const auto& mesh : model.meshes) {
            auto [min, max] = computeBounds(model, mesh);
            auto scale = (max - min) * 0.5f;
            auto position = (min + max) * 0.5f;
            auto transform = glm::translate(glm::mat4{1}, position) * glm::scale(glm::mat4{1}, scale);
            meshTransforms.push_back(transform);
        }

        std::vector<glm::mat4> placeHolders{};

        for(auto i = 0; i < model.nodes.size(); ++i) {
            const auto& node = model.nodes[i];
            if(node.mesh != -1) {
                auto instanceXform = transforms[i];
                auto baseXform = meshTransforms[node.mesh];
                placeHolders.push_back(instanceXform * baseXform);
            }
        }

        return placeHolders;
    }

    size_t getNumVertices(const tinygltf::Model& model, const tinygltf::Mesh& mesh) {
        size_t numVertices = 0;
        for(const auto& primitive : mesh.primitives) {
            numVertices += model.accessors[primitive.attributes.at("POSITION")].count;
        }
        return numVertices;
    }

    std::tuple<size_t, size_t, size_t> getNumIndices(const tinygltf::Model& model, const tinygltf::Mesh& mesh) {
        size_t u8  = 0;
        size_t u16 = 0;
        size_t u32 = 0;
        for(const auto& primitive : mesh.primitives) {
            const auto accessor = model.accessors[primitive.indices];
            if(to<ComponentType>(accessor.componentType) == ComponentType::UNSIGNED_BYTE) {
                u8 += accessor.count;
            }
            if(to<ComponentType>(accessor.componentType) == ComponentType::UNSIGNED_SHORT) {
                u16 += accessor.count;
            }
            if(to<ComponentType>(accessor.componentType) == ComponentType::UNSIGNED_INT) {
                u32 += accessor.count;
            }
        }

        return std::make_tuple(u32, u16, u8);
    }

    size_t getNumVertices(const tinygltf::Model& model, const tinygltf::Primitive& primitive) {
        return model.accessors[primitive.attributes.at("POSITION")].count;
    }

    int alphaModeToIndex(const std::string& mode) {
        if(mode == "OPAQUE") return 0;
        if(mode == "MASK") return 1;
        if(mode == "BLEND") return 2;

        throw std::runtime_error{"unknown alpha mode"};
    }

    void computeOffsets(const std::shared_ptr<PendingModel>& pending) {
        uint32_t vertexOffset = 0;
        uint32_t firstIndex8 = 0;
        uint32_t firstIndex16 = 0;
        uint32_t firstIndex32 = 0;

        for(const auto& mesh : pending->gltf->meshes) {
            pending->offsets.vertex.push_back(vertexOffset);
            pending->offsets.u8.push_back(firstIndex8);
            pending->offsets.u16.push_back(firstIndex16);
            pending->offsets.u32.push_back(firstIndex32);

            for(const auto& primitive : mesh.primitives){
                vertexOffset += getNumVertices(*pending->gltf, primitive);

                auto indexType = to<ComponentType>(pending->gltf->accessors[primitive.indices].componentType);

                if(indexType == ComponentType::UNSIGNED_BYTE) {
                    firstIndex16 += pending->gltf->accessors[primitive.indices].count;
                }else if(indexType == ComponentType::UNSIGNED_SHORT) {
                    firstIndex16 += pending->gltf->accessors[primitive.indices].count;
                }else {
                    firstIndex32 += pending->gltf->accessors[primitive.indices].count;
                }
            }
        }
    }

    std::vector<Camera> getCameras(const tinygltf::Model &model) {
        std::vector<Camera> cameras{};

        for(const auto& node : model.nodes) {
            if(node.camera != -1){
                const auto& gltfCamera = model.cameras[node.camera];
                Camera camera{};
                if(gltfCamera.type == "perspective") {
                    const auto& p = gltfCamera.perspective;
                    camera.proj =  vkn::perspectiveVFov(p.yfov, p.aspectRatio, p.znear, p.zfar);
                }else { // orthographic
                    const auto& o = gltfCamera.orthographic;
                    camera.proj = vkn::ortho(-o.xmag, o.xmag, -o.ymag, o.ymag, o.znear, o.zfar);
                }
                camera.view = glm::inverse(getTransformation(node));
                cameras.push_back(camera);
            }
        }
        return cameras;
    }

    ComponentType getComponentType(const tinygltf::Model &model, const tinygltf::Primitive &primitive, const std::string& attributeName) {
        if(!primitive.attributes.contains(attributeName)){
            return ComponentType::UNDEFINED;
        }
        return to<ComponentType>(model.accessors[primitive.attributes.at(attributeName)].componentType);
    }

    const std::map<int, VkFormat> Loader::channelFormatMap {
            {1, VK_FORMAT_R8_UNORM},
            {2, VK_FORMAT_R8G8_UNORM},
            {3, VK_FORMAT_R8G8B8_UNORM},
            {4, VK_FORMAT_R8G8B8A8_UNORM},
    };

    const std::vector<VkFormat> Loader::floatFormats {
        VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT
    };

    void Loader::handleError(Task &task, const std::exception& exception) {
        handleError(std::get_if<TextureUploadTask>(&task), exception);
    }

    void Loader::handleError(TextureUploadTask* textureUpload, const std::exception& exception) {
        if(!textureUpload) return;
        textureUpload->status->success = false;
        textureUpload->status->_ready.notifyAll();
        spdlog::error("Error uploading file {}, reason: {}", textureUpload->path, exception.what());
    }

    std::shared_ptr<Model> Loader::dummyModel() {
        auto dummyModel = std::make_shared<gltf::Model>();
        dummyModel->vertices = _device->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int));
        dummyModel->indices.u8.handle = _device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(uint16_t));
        dummyModel->indices.u16.handle = _device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(uint16_t));
        dummyModel->indices.u32.handle = _device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(uint32_t));
        dummyModel->materials = _device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int));
        dummyModel->textureInfos = _device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int));
        dummyModel->lights = _device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int));
        dummyModel->lightInstances = _device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int));
        dummyModel->meshes.u8.handle = _device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int));
        dummyModel->meshes.u16.handle = _device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int));
        dummyModel->meshes.u32.handle = _device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int));
        dummyModel->draw.u8.handle = _device->createBuffer(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int));
        dummyModel->draw.u16.handle = _device->createBuffer(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int));
        dummyModel->draw.u32.handle = _device->createBuffer(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int));
        dummyModel->meshDescriptorSet.u8.handle = _descriptorPool->allocate({ _descriptorSetLayout }).front();
        dummyModel->meshDescriptorSet.u16.handle = _descriptorPool->allocate({ _descriptorSetLayout }).front();
        dummyModel->meshDescriptorSet.u32.handle = _descriptorPool->allocate({ _descriptorSetLayout }).front();
        dummyModel->materialDescriptorSet = _descriptorPool->allocate({ _materialDescriptorSetLayout }).front();
        dummyModel->_sourceDescriptorPool = _descriptorPool;


        auto writes = initializers::writeDescriptorSets<7>();
        writes[0].dstSet = dummyModel->meshDescriptorSet.u8.handle;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        VkDescriptorBufferInfo meshBuffer8Info{ dummyModel->meshes.u8.handle, 0, VK_WHOLE_SIZE };
        writes[0].pBufferInfo = &meshBuffer8Info;

        writes[1].dstSet = dummyModel->meshDescriptorSet.u16.handle;
        writes[1].dstBinding = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        VkDescriptorBufferInfo meshBuffer16Info{ dummyModel->meshes.u16.handle, 0, VK_WHOLE_SIZE };
        writes[1].pBufferInfo = &meshBuffer16Info;

        writes[2].dstSet = dummyModel->meshDescriptorSet.u32.handle;
        writes[2].dstBinding = 0;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        VkDescriptorBufferInfo meshBufferInfo{ dummyModel->meshes.u32.handle, 0, VK_WHOLE_SIZE };
        writes[2].pBufferInfo = &meshBufferInfo;

        writes[3].dstSet = dummyModel->materialDescriptorSet;
        writes[3].dstBinding = 0;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].descriptorCount = 1;
        VkDescriptorBufferInfo materialBufferInfo{ dummyModel->materials, 0, VK_WHOLE_SIZE };
        writes[3].pBufferInfo = &materialBufferInfo;

        writes[4].dstSet = dummyModel->materialDescriptorSet;
        writes[4].dstBinding = 1;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].descriptorCount = 1;
        VkDescriptorBufferInfo lightBufferInfo{ dummyModel->lights, 0, VK_WHOLE_SIZE };
        writes[4].pBufferInfo = &lightBufferInfo;

        writes[5].dstSet = dummyModel->materialDescriptorSet;
        writes[5].dstBinding = 2;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].descriptorCount = 1;
        VkDescriptorBufferInfo lightInstanceBufferInfo{ dummyModel->lightInstances, 0, VK_WHOLE_SIZE };
        writes[5].pBufferInfo = &lightInstanceBufferInfo;

        writes[6].dstSet = dummyModel->materialDescriptorSet;
        writes[6].dstBinding = 3;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[6].descriptorCount = 1;
        VkDescriptorBufferInfo textureInfoBufferInfo{ dummyModel->textureInfos, 0, VK_WHOLE_SIZE };
        writes[6].pBufferInfo = &textureInfoBufferInfo;

        _device->updateDescriptorSets(writes);


        return dummyModel;
    }

    VulkanSampler Loader::createSampler(const tinygltf::Model& model, int sampler, uint32_t mipLevels) {

        auto getFilter = [](int filterId) {
            if(filterId == 9729) return VK_FILTER_LINEAR;
            return VK_FILTER_NEAREST;
        };

        auto getWrappingMode = [](int wrapId) {
            if(wrapId == 33071) return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            if(wrapId == 33648) return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        };

        VkSamplerCreateInfo samplerCreateInfo{};
        samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
        samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
        samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerCreateInfo.minLod = 0;
        samplerCreateInfo.maxLod = static_cast<float>(mipLevels - 1);

        if(sampler != -1) {
            const auto samplerInfo = model.samplers[sampler];
            samplerCreateInfo.magFilter = getFilter(samplerInfo.magFilter);
            samplerCreateInfo.minFilter = getFilter(samplerInfo.minFilter);
            samplerCreateInfo.addressModeU = getWrappingMode(samplerInfo.wrapS);
            samplerCreateInfo.addressModeV = getWrappingMode(samplerInfo.wrapT);
            samplerCreateInfo.addressModeW = getWrappingMode(samplerInfo.wrapS);
        }

        return _device->createSampler(samplerCreateInfo);
    }

    void Loader::onComplete(LightUploadTask* lightUpload) {
        if(!lightUpload) return;
        spdlog::info("light{} uploaded to gpu", lightUpload->LightId);
    }

    void Loader::onComplete(LightInstanceUploadTask* lightInstanceUpload) {
        if(!lightInstanceUpload) return;
        spdlog::info("lightInstance{} uploaded to gpu", lightInstanceUpload->instanceId);
    }

    TextureInfo Loader::extract(const tinygltf::TextureInfo &info, int offset) {
        if(info.index == -1) return {};

        TextureInfo tInfo{};
        tInfo.index = info.index + offset;
        tInfo.texCoord = info.texCoord;

        if(info.extensions.contains(KHR_texture_transform)) {
            const auto& transforms = info.extensions.at(KHR_texture_transform);

            if(transforms.Has("offset")) {
                tInfo.offset = vec2From(transforms.Get("offset"));
            }
            if(transforms.Has("scale")) {
                tInfo.scale = vec2From(transforms.Get("scale"));
            }

            if(transforms.Has("rotation")){
                tInfo.rotation = transforms.Get("rotation").GetNumberAsDouble();
            }

            if(transforms.Has("texCoord")) {
                tInfo.texCoord = transforms.Get("texCoord").GetNumberAsInt();
            }
        }

        return tInfo;
    }

    TextureInfo Loader::extract(const tinygltf::NormalTextureInfo &info, int offset) {
        if(info.index == -1) return {};

        TextureInfo tInfo{};
        tInfo.index = info.index + offset;
        tInfo.texCoord = info.texCoord;
        tInfo.tScale = info.scale;

        if(info.extensions.contains(KHR_texture_transform)) {
            const auto& transforms = info.extensions.at(KHR_texture_transform);

            if(transforms.Has("offset")) {
                tInfo.offset = vec2From(transforms.Get("offset"));
            }
            if(transforms.Has("scale")) {
                tInfo.scale = vec2From(transforms.Get("scale"));
            }

            if(transforms.Has("rotation")){
                tInfo.rotation = transforms.Get("rotation").GetNumberAsDouble();
            }

            if(transforms.Has("texCoord")) {
                tInfo.texCoord = transforms.Get("texCoord").GetNumberAsInt();
            }
        }

        return tInfo;
    }

    TextureInfo Loader::extract(const tinygltf::OcclusionTextureInfo &info, int offset) {
        if(info.index == -1) return {};

        TextureInfo tInfo{};
        tInfo.index = info.index + offset;
        tInfo.texCoord = info.texCoord;
        tInfo.tScale = info.strength;

        if(info.extensions.contains(KHR_texture_transform)) {
            const auto& transforms = info.extensions.at(KHR_texture_transform);

            if(transforms.Has("offset")) {
                tInfo.offset = vec2From(transforms.Get("offset"));
            }
            if(transforms.Has("scale")) {
                tInfo.scale = vec2From(transforms.Get("scale"));
            }

            if(transforms.Has("rotation")){
                tInfo.rotation = transforms.Get("rotation").GetNumberAsDouble();
            }

            if(transforms.Has("texCoord")) {
                tInfo.texCoord = transforms.Get("texCoord").GetNumberAsInt();
            }
        }

        return tInfo;
    }

    void Loader::extractSheen(MaterialData &material, MaterialUploadTask& materialUpload) {
        const auto& gMat = materialUpload.material;
        if(!gMat.extensions.contains(KHR_materials_sheen)) return;

        const auto& sheen = gMat.extensions.at(KHR_materials_sheen);
        if(sheen.Has("sheenColorFactor")){
            material.sheenColorFactor = vec3From(sheen.Get("sheenColorFactor"));
        }
        if(sheen.Has("sheenRoughnessFactor")){
            material.sheenRoughnessFactor = to<float>(sheen.Get("sheenRoughnessFactor").GetNumberAsDouble());
        }

        if(sheen.Has("sheenColorTexture")) {
            const auto& colorTexture = extract(sheen.Get("sheenColorTexture"), materialUpload.textureOffset);
            const auto& roughnessTexture = extract(sheen.Get("sheenColorTexture"), materialUpload.textureOffset);
            materialUpload.textureInfos[to<int>(TextureType::SHEEN_COLOR)] = colorTexture;
            materialUpload.textureInfos[to<int>(TextureType::SHEEN_ROUGHNESS)] = roughnessTexture;
        }
    }

    TextureInfo Loader::extract(const tinygltf::Value &v, int offset) {
        TextureInfo info{};

        info.index = v.Has("index") ? v.Get("index").GetNumberAsInt() + offset : -1;
        info.texCoord = v.Has("texCoord") ? v.Get("texCoord").GetNumberAsInt() : 0;

        if(v.Has("extensions") && v.Get("extensions").Has(KHR_texture_transform)) {
            const auto transforms = v.Get("extensions").Get(KHR_texture_transform);

            if(transforms.Has("offset")) {
                info.offset = vec2From(transforms.Get("offset"));
            }
            if(transforms.Has("scale")) {
                info.scale = vec2From(transforms.Get("scale"));
            }

            if(transforms.Has("rotation")){
                info.rotation = transforms.Get("rotation").GetNumberAsDouble();
            }

            if(transforms.Has("texCoord")) {
                info.texCoord = transforms.Get("texCoord").GetNumberAsInt();
            }

        }
        return info;
    }
}