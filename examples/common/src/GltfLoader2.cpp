#include "gltf/GltfLoader2.hpp"
#include "Vertex.h"
#include <spdlog/spdlog.h>

#include <utility>

namespace gltf2 {

    struct Counts {
        struct { size_t u16{}; size_t u32{}; size_t count() const { return u16 + u32; }} instances;
        struct { size_t u16{}; size_t u32{}; size_t count() const { return u16 + u32; }} indices;
        size_t primitives{};
        size_t vertices{};
    };

    glm::vec2 vec2From(const std::vector<double>& v);
    glm::vec3 vec3From(const std::vector<double>& v);
    glm::vec4 vec4From(const std::vector<double>& v);
    glm::quat quaternionFrom(const std::vector<double>& q);
    glm::mat4 getTransformation(const tinygltf::Node& node);


    size_t getNumVertices(const tinygltf::Model& model, const tinygltf::Primitive& primitive);

    size_t getNumVertices(const tinygltf::Model& model, const tinygltf::Mesh& mesh);

    std::tuple<size_t, size_t> getNumIndices(const tinygltf::Model& model, const tinygltf::Mesh& mesh);

    void tinyGltfLoad(tinygltf::Model& model, const std::string& path);

    Counts getCounts(const tinygltf::Model& model);

    std::vector<glm::mat4> computeTransforms(const tinygltf::Model& model);

    std::vector<glm::mat4> computePlaceHolders(const tinygltf::Model& model, const std::vector<glm::mat4>& transforms);

    std::tuple<glm::vec3, glm::vec3> computeBounds(const tinygltf::Model& model, const tinygltf::Mesh& mesh);

    std::tuple<glm::vec3, glm::vec3> computeBounds(const tinygltf::Model& model, const std::vector<glm::mat4>& transforms);

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
    void calculateTangents(std::vector<Vertex>& vertices, std::vector<indexType> indices) {
        for(int i = 0; i < indices.size(); i+= 3){
            auto& v0 = vertices[indices[i]];
            auto& v1 = vertices[indices[i+1]];
            auto& v2 = vertices[indices[i+2]];

            auto e1 = v1.position.xyz() - v0.position.xyz();
            auto e2 = v2.position.xyz() - v0.position.xyz();

            auto du1 = v1.uv.x - v0.uv.x;
            auto dv1 = v1.uv.y - v0.uv.y;
            auto du2 = v2.uv.x - v0.uv.x;
            auto dv2 = v2.uv.y - v0.uv.y;

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
    , _barrierObjectPools(numWorkers, BufferMemoryBarrierPool(1024) )
    , _imageMemoryBarrierObjectPools(numWorkers, ImageMemoryBarrierPool(1024) )
    , _bufferCopyPool( numWorkers, BufferCopyPool(1024))
    , _bufferImageCopyPool( numWorkers, BufferImageCopyPool(1024))
    {}

    void Loader::start() {
        initPlaceHolders();
        createDescriptorSetLayout();
        initCommandPools();
        _staging.buffer = _device->createStagingBuffer(stagingBufferSize);
        _fence = _device->createFence();
        _running = true;

        for(auto i = 0; i < _workerCount; ++i){
            std::thread worker{ [workerId = i, this]{ workerLoop(workerId); } };
            _workers.push_back(std::move(worker));
        }

        _coordinator = std::thread{ [this]{ coordinatorLoop(); } };
        spdlog::info("GLTF model loader started");
    }

    std::shared_ptr<Model> Loader::load(const std::filesystem::path &path) {
        if(_pendingModels.full()) {
            spdlog::warn("loader at capacity");
            return {};
        }

        auto gltf  = std::make_unique<tinygltf::Model>();
        tinyGltfLoad(*gltf, path.string());
        const auto counts = getCounts(*gltf);


        auto model = std::make_shared<Model>();
        model->numMeshes = counts.instances.count();
        model->numTextures = gltf->textures.size();

        VkDeviceSize byteSize = sizeof(Vertex) * counts.vertices;
        model->vertices = _device->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_vertices", _modelId));

        byteSize =  sizeof(uint16_t) + sizeof(uint16_t) * counts.indices.u16;
        model->indices.u16.handle = _device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_indices_u16", _modelId));

        byteSize = sizeof(uint32_t) + sizeof(uint32_t) * counts.indices.u32;
        model->indices.u32.handle = _device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT , VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_indices_u32", _modelId));

        byteSize = counts.instances.u16 * sizeof(VkDrawIndexedIndirectCommand) + sizeof(VkDrawIndexedIndirectCommand);
        model->draw.u16.handle = _device->createBuffer(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_draw_u16", _modelId));
        model->draw.u16.count = 0;

        byteSize = counts.instances.u32 * sizeof(VkDrawIndexedIndirectCommand) + sizeof(VkDrawIndexedIndirectCommand);
        model->draw.u32.handle = _device->createBuffer(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_draw_u32", _modelId));
        model->draw.u32.count = 0;

        byteSize = sizeof(MeshData) * counts.instances.u16 + sizeof(MeshData);
        model->meshes.u16.handle = _device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_instance_u16", _modelId));

        byteSize = sizeof(MeshData) * counts.instances.u32 + sizeof(MeshData);
        model->meshes.u32.handle = _device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, byteSize, fmt::format("model{}_instance_u32", _modelId));


        std::vector<MaterialData> materials(gltf->materials.size());
        for(auto& material : materials) {
            material.textures.fill(-1);
        }
        model->materials = _device->createDeviceLocalBuffer(materials.data(), BYTE_SIZE(materials), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        _device->setName<VK_OBJECT_TYPE_BUFFER>(fmt::format("mode{}_materials", _modelId), model->materials.buffer);

        auto transforms = computeTransforms(*gltf);
        auto [bMin, bMax] = computeBounds(*gltf, transforms);
        auto sets = _descriptorPool->allocate({_descriptorSetLayout, _descriptorSetLayout, _descriptorSetLayout });
        model->bounds.min = bMin;
        model->bounds.max = bMax;
        model->meshDescriptorSet.u16.handle = sets[0];
        model->meshDescriptorSet.u32.handle = sets[1];
        model->materialDescriptorSet = sets[2];
        model->placeHolders =  computePlaceHolders(*gltf, transforms);

        auto writes = initializers::writeDescriptorSets<3>();
        writes[0].dstSet = model->meshDescriptorSet.u16.handle;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        VkDescriptorBufferInfo meshBuffer16Info{ model->meshes.u16.handle, 0, VK_WHOLE_SIZE };
        writes[0].pBufferInfo = &meshBuffer16Info;

        writes[1].dstSet = model->meshDescriptorSet.u32.handle;
        writes[1].dstBinding = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        VkDescriptorBufferInfo meshBufferInfo{ model->meshes.u32.handle, 0, VK_WHOLE_SIZE };
        writes[1].pBufferInfo = &meshBufferInfo;

        writes[2].dstSet = model->materialDescriptorSet;
        writes[2].dstBinding = 0;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        VkDescriptorBufferInfo materialBufferInfo{ model->materials, 0, VK_WHOLE_SIZE };
        writes[2].pBufferInfo = &materialBufferInfo;

        _device->updateDescriptorSets(writes);

        const auto textureBindingOffset = _bindlessDescriptor->reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, gltf->textures.size());
        for(auto i = 0; i < gltf->textures.size(); ++i) {
            const auto bindingId = static_cast<uint32_t>(i + textureBindingOffset);
            _bindlessDescriptor->update({ &_placeHolderTexture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, bindingId});
        }

        auto pendingModel = std::make_shared<PendingModel>();
        pendingModel->device = _device;
        pendingModel->bindlessDescriptor = _bindlessDescriptor;
        pendingModel->gltf = std::move(gltf);
        pendingModel->model = model;
        pendingModel->transforms = transforms;
        pendingModel->textureBindingOffset = textureBindingOffset;

        _pendingModels.push(pendingModel);
        _modelLoadPending.signal();

        return model;

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
    }

    void Loader::coordinatorLoop() {
        std::stack<Task> completedTasks;
        std::vector<VkCommandBuffer> commandBuffers;

        while(_running) {
            _modelLoadPending.wait([&]{ return !_pendingModels.empty() || !_commandBufferQueue.empty() || !_running; });

            if(!_running) {
                _workerQueue.broadcast(StopWorkerTask{});
                _taskPending.signalAll();
                for(auto& worker : _workers) {
                    worker.join();
                }
            }

            while(!_commandBufferQueue.empty()) {
                auto result = _commandBufferQueue.pop();
                commandBuffers.insert( commandBuffers.end(), result.commandBuffers.begin(), result.commandBuffers.end());
                completedTasks.push(std::move(result.task));
            }

            if(!commandBuffers.empty()){
                spdlog::info("executing {} commandBuffers", commandBuffers.size());
                execute(commandBuffers);
                while(!completedTasks.empty()){
                    auto task = completedTasks.top();
                    onComplete(task);
                    completedTasks.pop();
                }
                _taskPending.signalAll();
            }

            commandBuffers.clear();

            while(!_pendingModels.empty()) {
                auto pending = _pendingModels.pop();

                for(auto i = 0u; i < pending->gltf->meshes.size(); ++i) {
                    auto&  mesh = pending->gltf->meshes[i];
                    _workerQueue.push(MeshUploadTask{ pending, mesh, i});
                }

                for(auto i = 0; i < pending->gltf->textures.size(); ++i) {
                    uint32_t bindingId = i + pending->textureBindingOffset;
                    auto& texture = pending->gltf->textures[i];
                    _workerQueue.push(TextureUploadTask{ pending, texture, bindingId });
                }
//
//                for(auto& material : pending->gltf->materials) {
//                    _workerQueue.push(MaterialUploadTask{ pending, material});
//                }
                _taskPending.signalAll();
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
        while(running) {
            _taskPending.wait( [&]{ return !workQueue->empty(); } );

            while(!workQueue->empty()) {
                auto task = workQueue->pop();
                if (std::get_if<StopWorkerTask>(&task)) {
                    running = false;
                    break;
                }
                auto commandBuffer = processTask(task, id);

                _commandBufferQueue.push({{commandBuffer}, task});
                _modelLoadPending.signal();
            }
        }
        spdlog::info("GLTF worker[{}] going offline", id);
    }

    VkCommandBuffer Loader::processTask(Task& task, int workerId) {
        auto commandBuffer = _workerCommandPools[workerId].allocate(VK_COMMAND_BUFFER_LEVEL_SECONDARY);

        auto beginInfo = initializers::commandBufferBeginInfo();
        static VkCommandBufferInheritanceInfo inheritanceInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO };
        beginInfo.pInheritanceInfo = &inheritanceInfo;
        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        process(commandBuffer, std::get_if<MeshUploadTask>(&task), workerId);
        process(commandBuffer, std::get_if<TextureUploadTask>(&task), workerId);
        process(commandBuffer, std::get_if<MaterialUploadTask>(&task), workerId);
        process(commandBuffer, std::get_if<InstanceUploadTask>(&task), workerId);

        vkEndCommandBuffer(commandBuffer);


        return commandBuffer;
    }

    void Loader::process(VkCommandBuffer commandBuffer, MeshUploadTask* meshUpload, int workerId) {
        if(!meshUpload) return;
//        spdlog::info("worker{} processing mesh upload", workerId);

        _barrierObjectPools[workerId].releaseAll();

        auto pending = meshUpload->pending;
        auto model = pending->model;

        auto vertexCount = getNumVertices(*pending->gltf, meshUpload->mesh);
        auto [u32, u16] = getNumIndices(*pending->gltf, meshUpload->mesh);

        uint32_t vertexOffset = _offsets.vertexOffset.fetch_add(vertexCount);
        uint32_t firstIndex16 = _offsets.firstIndex16.fetch_add(u16);
        uint32_t firstIndex32 = _offsets.firstIndex32.fetch_add(u32);

        spdlog::info("worker{}, {}, {}, {}", workerId, vertexOffset, firstIndex16, firstIndex32);

        std::array<VkBufferCopy, 3> regions{};
        regions[0].dstOffset = vertexOffset * sizeof(Vertex);
        regions[1].dstOffset = firstIndex16 * sizeof(uint16_t);
        regions[2].dstOffset = firstIndex32 * sizeof(uint32_t);

        
        for(const auto& primitive : meshUpload->mesh.primitives) {
            auto indexType = ComponentTypes::valueOf(pending->gltf->accessors[primitive.indices].componentType);
            const auto numVertices = getNumVertices(*pending->gltf, primitive);
            std::vector<Vertex> vertices;
            vertices.reserve(numVertices);

            VkDeviceSize indicesByteSize = 0;
            if(ComponentTypes::valueOf(pending->gltf->accessors[primitive.indices].componentType) == ComponentType::UNSIGNED_SHORT) {
                indicesByteSize = pending->gltf->accessors[primitive.indices].count * sizeof(uint16_t);
            }else {
                indicesByteSize = pending->gltf->accessors[primitive.indices].count * sizeof(uint32_t);
            }
            auto stagingBuffer = _device->createStagingBuffer(numVertices * sizeof(Vertex) + indicesByteSize);
            _stagingRefs.push_back(stagingBuffer);

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

            regions[0].size = BYTE_SIZE(vertices);

            if(indexType == ComponentType::UNSIGNED_SHORT) {
                auto pIndices = getIndices<uint16_t>(*pending->gltf, primitive);
                std::vector<uint16_t> indices{pIndices.begin(), pIndices.end()};
                regions[1].srcOffset = regions[0].size;
                regions[1].size = BYTE_SIZE(indices);
                stagingBuffer.copy(indices, regions[1].srcOffset);

                if(tangents.empty()) {
                    calculateTangents(vertices, indices);
                }
            }else {
                auto pIndices = getIndices<uint32_t>(*pending->gltf, primitive);
                std::vector<uint32_t> indices{pIndices.begin(), pIndices.end()};
                regions[2].srcOffset = regions[0].size;
                regions[2].size = BYTE_SIZE(indices);
                stagingBuffer.copy(indices, regions[2].srcOffset);

                if(tangents.empty()) {
                    calculateTangents(vertices, indices);
                }
            }

            stagingBuffer.copy(vertices, 0);


            assert(model->vertices.size >= regions[0].dstOffset);
            vkCmdCopyBuffer(commandBuffer, stagingBuffer, model->vertices, 1, &regions[0]);

            if(indexType == ComponentType::UNSIGNED_SHORT) {
                assert(model->indices.u16.handle.size >= regions[1].dstOffset);
                vkCmdCopyBuffer(commandBuffer, stagingBuffer, model->indices.u16.handle, 1, &regions[1]);
            }else {
                assert(model->indices.u32.handle.size >= regions[2].dstOffset);
                vkCmdCopyBuffer(commandBuffer, stagingBuffer, model->indices.u32.handle, 1, &regions[2]);
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

            if(indexType == ComponentType::UNSIGNED_SHORT) {
                indexBarrier.offset = regions[1].dstOffset;
                indexBarrier.buffer = model->indices.u16.handle;
                indexBarrier.size = regions[1].size;
            }else {
                indexBarrier.offset = regions[2].dstOffset;
                indexBarrier.buffer = model->indices.u32.handle;
                indexBarrier.size = regions[2].size;
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
                    .indexType = indexType
            };
            meshUpload->primitives.push_back(mesh);

            if(indexType == ComponentType::UNSIGNED_SHORT) {
                firstIndex16 += mesh.indexCount;
            }else if(indexType == ComponentType::UNSIGNED_INT){
                firstIndex32 += mesh.indexCount;
            }
            vertexOffset += vertices.size();
            regions[0].dstOffset += regions[0].size;

            if(indexType == ComponentType::UNSIGNED_SHORT) {
                regions[1].dstOffset += regions[1].size;
            }else {
                regions[2].dstOffset += regions[2].size;
            }
        }
    }

    void Loader::process(VkCommandBuffer commandBuffer, TextureUploadTask* textureUpload, int workerId) {
        if(!textureUpload) return;

        static std::vector<VulkanBuffer> stagingBufferRef; // FIXME, used pooled memory to allocate staging buffer

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
        int textureId = 0;

        auto& region = *_bufferImageCopyPool[workerId].acquire();
        VkDeviceSize bufferOffset = 0;

        Texture texture;
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

        auto stagingBuffer = _device->createStagingBuffer(image.image.size());
        stagingBufferRef.push_back(stagingBuffer);  // FIXME remove this hack
        stagingBuffer.copy(image.image);  // TODO retrieve data from BufferView if uri.empty() == true

        prepTransferBarrier.image = texture.image.image;
        prepTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        prepTransferBarrier.subresourceRange = subresourceRange;
        prepTransferBarrier.srcAccessMask = VK_ACCESS_NONE;
        prepTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        prepTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        prepTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &prepTransferBarrier);

        region.bufferOffset = bufferOffset;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = { texture.width, texture.height, 1 };
        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.buffer, texture.image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

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

        textureUpload->pending->model->textures.push_back(std::move(texture));      // FIXME not thread safe
        textureUpload->gpuTexture = &textureUpload->pending->model->textures.back(); // FIXME not thread safe
    }

    void Loader::process(VkCommandBuffer, MaterialUploadTask* materialUpload, int workerId) {
        if(!materialUpload) return;
//        spdlog::info("worker{} processing material upload", workerId);
    }

    void Loader::process(VkCommandBuffer commandBuffer, InstanceUploadTask* instanceUpload, int workerId) {
        if(!instanceUpload) return;

//        _barrierObjectPools[workerId].releaseAll();
//        _bufferCopyPool[workerId].releaseAll();

        spdlog::info("worker{} processing Instance upload", workerId);

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

        spdlog::info("meshInstances: {}", meshInstances);

        struct {
            std::vector<VkDrawIndexedIndirectCommand> u16;
            std::vector<VkDrawIndexedIndirectCommand> u32;
        } drawCommands;

        struct {
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

                    if(mesh.indexType == ComponentType::UNSIGNED_SHORT) {
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
            uint32_t u16{};
            uint32_t u32{};
        } drawOffset;

        drawOffset.u16 = instanceUpload->pending->drawOffset.u16.fetch_add(drawCommands.u16.size());
        drawOffset.u32 = instanceUpload->pending->drawOffset.u32.fetch_add(drawCommands.u32.size());

        Buffer drawCmdStaging{};
        drawCmdStaging.u16.handle = _device->createStagingBuffer(BYTE_SIZE(drawCommands.u16) + sizeof(VkDrawIndexedIndirectCommand));
        drawCmdStaging.u32.handle = _device->createStagingBuffer(BYTE_SIZE(drawCommands.u32) + sizeof(VkDrawIndexedIndirectCommand));

        Buffer meshStaging{};
        meshStaging.u16.handle = _device->createStagingBuffer(BYTE_SIZE(meshData.u16) + sizeof(MeshData));
        meshStaging.u32.handle = _device->createStagingBuffer(BYTE_SIZE(meshData.u32) + sizeof(MeshData));

        _stagingRefs.push_back(meshStaging.u16.handle);
        _stagingRefs.push_back(meshStaging.u32.handle);
        _stagingRefs.push_back(drawCmdStaging.u32.handle);
        _stagingRefs.push_back(drawCmdStaging.u32.handle);

        const auto model = instanceUpload->pending->model;
        if(!drawCommands.u16.empty()) {
            meshStaging.u16.handle.copy(meshData.u16);
            drawCmdStaging.u16.handle.copy(drawCommands.u16);
            transferMeshInstance(commandBuffer, drawCmdStaging.u16.handle, model->draw.u16.handle, meshStaging.u16.handle, model->meshes.u16.handle, drawOffset.u16, workerId);
        }

        if(!drawCommands.u32.empty()) {
            meshStaging.u32.handle.copy(meshData.u32);
            drawCmdStaging.u32.handle.copy(drawCommands.u32);
            transferMeshInstance(commandBuffer, drawCmdStaging.u32.handle, model->draw.u32.handle, meshStaging.u32.handle, model->meshes.u32.handle, drawOffset.u32, workerId);

        }

        instanceUpload->drawCounts.u16 = drawCommands.u16.size();
        instanceUpload->drawCounts.u32 = drawCommands.u32.size();
    }

    void Loader::transferMeshInstance(VkCommandBuffer commandBuffer, VulkanBuffer &drawCmdSrc, VulkanBuffer &drawCmdDst, VulkanBuffer &meshDataSrc, VulkanBuffer &meshDataDst, uint32_t drawOffset, int workerId) {
        VkDeviceSize meshOffset = drawOffset * sizeof(MeshData);
        VkDeviceSize drawCmdOffset = drawOffset * sizeof(VkDrawIndexedIndirectCommand);

        assert(drawCmdSrc.size + drawCmdOffset <= drawCmdDst.size);
        assert(meshDataSrc.size + meshOffset <= meshDataDst.size);

        VkBufferCopy& meshRegion = *_bufferCopyPool[workerId].acquire();
        meshRegion.srcOffset = 0;
        meshRegion.dstOffset = meshOffset;
        meshRegion.size = meshDataSrc.size;


        vkCmdCopyBuffer(commandBuffer, meshDataSrc, meshDataDst, 1, &meshRegion);

        VkBufferCopy& drawRegion = *_bufferCopyPool[workerId].acquire();
        drawRegion.srcOffset = 0;
        drawRegion.dstOffset = drawCmdOffset;
        drawRegion.size = drawCmdSrc.size;


        vkCmdCopyBuffer(commandBuffer, drawCmdSrc, drawCmdDst, 1, &drawRegion);

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

    BufferRegion Loader::allocate(VkDeviceSize size) {
        if(_staging.offset + size > _staging.buffer.size) {
            throw std::runtime_error{ "ran out of staging memory" };
        }
        const auto start = _staging.offset;
        const auto end = start + size;
        _staging.offset += size;
        return _staging.buffer.region(start, end);
    }

    void Loader::stop() {
        _running = false;
        _modelLoadPending.signal();
        _coordinator.join();
    }

    void Loader::initCommandPools() {
        for(auto i = 0; i < _workerCount; ++i) {
            _workerCommandPools.push_back(_device->createCommandPool(*_device->queueFamilyIndex.transfer, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT));
        }
    }

    void Loader::onComplete(Task& task) {
        onComplete(std::get_if<MeshUploadTask>(&task));
        onComplete(std::get_if<InstanceUploadTask>(&task));
        onComplete(std::get_if<TextureUploadTask>(&task));
    }

    void Loader::onComplete(MeshUploadTask *meshUpload) {
        if(!meshUpload) return;
        _workerQueue.push(InstanceUploadTask{ meshUpload->pending, meshUpload->primitives});
    }

    void Loader::onComplete(InstanceUploadTask *instanceUpload) {
        if(!instanceUpload) return;
        auto model = instanceUpload->pending->model;
        model->draw.u16.count += instanceUpload->drawCounts.u16;
        model->draw.u32.count += instanceUpload->drawCounts.u32;

        if((model->draw.u16.count + model->draw.u32.count) == model->numMeshes) {
            model->_loaded.signal();
        }

        spdlog::info("i32: {}, 116: {}", instanceUpload->pending->model->draw.u32.count, instanceUpload->pending->model->draw.u16.count);
    }

    void Loader::onComplete(TextureUploadTask *textureUpload) {
        if(!textureUpload) return;
        spdlog::info("texture upload complete..");
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

    Counts getCounts(const tinygltf::Model &model) {
        Counts counts{};

        for(const auto& mesh : model.meshes) {
            for(const auto& primitive : mesh.primitives) {
                ++counts.primitives;
                counts.vertices += model.accessors[primitive.attributes.at("POSITION")].count;
                if (model.accessors[primitive.indices].componentType == static_cast<int>(ComponentType::UNSIGNED_SHORT)) {
                    counts.indices.u16 += model.accessors[primitive.indices].count;
                } else if (model.accessors[primitive.indices].componentType == static_cast<int>(ComponentType::UNSIGNED_INT)) {
                    counts.indices.u32 += model.accessors[primitive.indices].count;
                }
            }
        }

        for(const auto& node : model.nodes) {
            if(node.mesh != -1){
                for(const auto& prim : model.meshes[node.mesh].primitives) {
                    if(model.accessors[prim.indices].componentType == static_cast<int>(ComponentType::UNSIGNED_SHORT)) {
                        counts.instances.u16++;
                    }else if(model.accessors[prim.indices].componentType == static_cast<int>(ComponentType::UNSIGNED_INT)){
                        counts.instances.u32++;
                    }
                }
            }
        }

        return counts;
    }

    std::vector<glm::mat4> computeTransforms(const tinygltf::Model &model) {
        auto numNodes = model.nodes.size();
        std::vector<glm::mat4> transforms(numNodes, glm::mat4{1});

        for(auto i = 0; i < numNodes; ++i) {
            transforms[i] = getTransformation(model.nodes[i]);
        }
        
        for(auto nodeId : model.scenes[0].nodes) {
            const auto& node = model.nodes[nodeId];
            if(!node.children.empty()) {
                for(auto childId : node.children) {
                    transforms[childId] = transforms[nodeId] * transforms[childId];
                }
            }
        }

        return transforms;
    }

    glm::mat4 getTransformation(const tinygltf::Node &node) {
        glm::mat4 transform{glm::vec4{}, glm::vec4{}, glm::vec4{}, glm::vec4{}};
        if(!node.matrix.empty()) {
            assert(false || "node matrix extraction not yet implemented!");
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

    std::tuple<size_t, size_t> getNumIndices(const tinygltf::Model& model, const tinygltf::Mesh& mesh) {
        size_t u16 = 0;
        size_t u32 = 0;
        for(const auto& primitive : mesh.primitives) {
            const auto accessor = model.accessors[primitive.indices];
            if(ComponentTypes::valueOf(accessor.componentType) == ComponentType::UNSIGNED_SHORT) {
                u16 += accessor.count;
            }
            if(ComponentTypes::valueOf(accessor.componentType) == ComponentType::UNSIGNED_INT) {
                u32 += accessor.count;
            }
        }

        return std::make_tuple(u32, u16);
    }

    size_t getNumVertices(const tinygltf::Model& model, const tinygltf::Primitive& primitive) {
        return model.accessors[primitive.attributes.at("POSITION")].count;
    }


}