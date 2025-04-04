#include "AsyncModelLoader.hpp"

#include <spdlog/spdlog.h>
#include <assimp/postprocess.h>
#include <openvdb/openvdb.h>
#include <openvdb/io/Stream.h>

#include <functional>

namespace asyncml {

    constexpr uint32_t DEFAULT_PROCESS_FLAGS = aiProcess_GenSmoothNormals | aiProcess_Triangulate
                                               | aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices
                                               | aiProcess_ValidateDataStructure;

//    constexpr uint32_t DEFAULT_PROCESS_FLAGS = aiProcess_CalcTangentSpace;

    constexpr VkDeviceSize _64MB = 64 * 1024 * 1024;

//    void visit(const aiNode* node, auto visitor) {
//        if(node == nullptr) return;
//        visitor(node);
//        for(auto i = 0; i < node->mNumChildren; i++){
//            visit(node->mChildren[i], visitor);
//        }
//    }

    uint32_t getNumVertices(const aiScene* scene) {
        uint32_t numVertices = 0;
        for(auto i = 0; i < scene->mNumMeshes; i++) {
            numVertices += scene->mMeshes[i]->mNumVertices;
        }
        return numVertices;
    }

    uint32_t getNumIndices(const aiScene* scene) {
        uint32_t numIndices = 0;
        for(auto i = 0; i < scene->mNumMeshes; i++) {
            const auto mesh = scene->mMeshes[i];
            for(auto j = 0; j < mesh->mNumFaces; j++){
                numIndices += mesh->mFaces[j].mNumIndices;
            }
        }
        return numIndices;
    }


    Loader::Loader(VulkanDevice *device, size_t reserve)
    : _device{device}
    , _pendingModels{reserve}
    , _pendingVolumes{reserve}
    , _pendingUploads(1024)
    , _pendingVolumeUploads(1024)
    {}

    void Loader::start() {
        openvdb::initialize();
        _stagingBuffer = _device->createStagingBuffer(_64MB, { *_device->queueFamilyIndex.transfer });
        _fence = _device->createFence();
        _running = true;
        _thread = std::thread{[this] { execLoop(); }};
        spdlog::info("model loader started");
    }

    std::shared_ptr<Model> asyncml::Loader::load(const std::filesystem::path &path, float unit) {
        if(_pendingModels.full()) {
            return {};
        }
        const auto queueFamilyIndex = *_device->queueFamilyIndex.transfer;

        PendingModel pending{.model = std::make_shared<Model>(), .scene = _importer.ReadFile(path.string(), DEFAULT_PROCESS_FLAGS), .path = path, .unit = unit};
        const auto numMeshes = pending.scene->mNumMeshes;
        pending.model->meshes.reset(numMeshes);
        pending.model->uploadedTextures.reset(1024);
        pending.model->draw.gpu = _device->createBuffer(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
                                                        , VMA_MEMORY_USAGE_CPU_TO_GPU, numMeshes * sizeof(VkDrawIndexedIndirectCommand));
        pending.model->draw.cpu = reinterpret_cast<VkDrawIndexedIndirectCommand*>(pending.model->draw.gpu.map());
        pending.model->draw.count = 0;

        const auto numVertices = getNumVertices(pending.scene);
        pending.model->vertexBuffer = _device->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                                                            , VMA_MEMORY_USAGE_GPU_ONLY, sizeof(Vertex) * numVertices, "", { queueFamilyIndex  });

        const auto numIndices = getNumIndices(pending.scene);
        pending.model->indexBuffer = _device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                                                         , VMA_MEMORY_USAGE_GPU_ONLY, sizeof(uint32_t) * numIndices, "", { queueFamilyIndex  });

        std::vector<MeshData> meshes(numMeshes);
        pending.model->meshBuffer = _device->createCpuVisibleBuffer(meshes.data(), BYTE_SIZE(meshes), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        std::vector<MaterialData> materials(pending.scene->mNumMaterials);
        pending.model->materialBuffer = _device->createCpuVisibleBuffer(materials.data(), BYTE_SIZE(materials), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        _pendingModels.push(pending);
        _loadRequest.notify_one();

        spdlog::info("loading model from {}, containing {} meshes with {} vertices", path.string(), numMeshes, numVertices);

        return pending.model;
    }

    void Loader::execLoop() {
        while(_running) {
            using namespace std::chrono_literals;
            {
                std::unique_lock<std::mutex> lk{_mutex};
                _loadRequest.wait(lk, [&]{
                    return !_pendingModels.empty()
                        || !_pendingVolumes.empty()
                        || !_pendingUploads.empty()
                        || !_pendingVolumeUploads.empty();
                });
            }
            if(!_running) break;
            uploadMeshes();
            uploadTextures();
            processVolumes();
            uploadVolumes();
        }
        spdlog::info("async loader offline");
    }

    void Loader::uploadMeshes() {
        const auto queue = _device->queues.transfer;
        const auto& commandPool = _device->createCommandPool(*_device->queueFamilyIndex.transfer, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
        auto commandBuffer = commandPool.allocate();

        while(!_pendingModels.empty()) {
            auto pending = _pendingModels.pop();
            const auto scene = pending.scene;
            const auto unit = pending.unit;
            auto model = pending.model;

            std::vector<aiMesh*> meshes;
            meshes.reserve(scene->mNumMeshes);
            for(auto i = 0; i < scene->mNumMeshes; i++) {
                meshes.push_back(scene->mMeshes[i]);
            }

            uint32_t vertexOffset = 0;
            uint32_t firstIndex = 0;
            std::array<VkBufferCopy, 2> regions{};

            for(auto meshId = 0; meshId < meshes.size(); meshId++) {
                const auto aiMesh = meshes[meshId];
                const glm::vec4 defaultAlbedo{0.6, 0.6, 0.6, 1.0};

                std::vector<Vertex> vertices;
                vertices.reserve(aiMesh->mNumVertices);
                for(auto i = 0; i < aiMesh->mNumVertices; i++) {
                    Vertex vertex{};
                    auto aiVec = aiMesh->mVertices[i];

                    vertex.position = {aiVec.x * unit, aiVec.y * unit, aiVec.z * unit, 1.0f};

                    if(aiMesh->HasNormals()){
                        auto aiNorm = aiMesh->mNormals[i];
                        vertex.normal = {aiNorm.x, aiNorm.y, aiNorm.z};
                    }

                    if(aiMesh->HasTangentsAndBitangents()){
                        auto aiTan = aiMesh->mTangents[i];
                        auto aiBi = aiMesh->mBitangents[i];
                        vertex.tangent = {aiTan.x, aiTan.y, aiTan.z};
                        vertex.bitangent = {aiBi.x, aiBi.y, aiBi.z};
                    }

                    if(aiMesh->HasVertexColors(0)){
                        auto aiCol = aiMesh->mColors[i][0];
                        vertex.color = {aiCol.r, aiCol.g, aiCol.b, aiCol.a};
                    }else {
                        vertex.color = defaultAlbedo;
                    }

                    if(aiMesh->HasTextureCoords((0))){  // TODO retrieve up to AI_MAX_NUMBER_OF_TEXTURECOORDS textures coordinates
                        auto aiTex = aiMesh->mTextureCoords[0][i];
                        vertex.uv = {aiTex.x, aiTex.y};
                    }
                    vertices.push_back(vertex);
                }

                std::vector<uint32_t> indexes;
                indexes.reserve(aiMesh->mNumFaces * 3);
                for(auto i = 0; i < aiMesh->mNumFaces; i++){
                    for(auto j = 0; j < aiMesh->mFaces[i].mNumIndices; j++) {
                        indexes.push_back(aiMesh->mFaces[i].mIndices[j]);
                    }
                }

                regions[0].size = vertices.size() * sizeof(Vertex);
                _stagingBuffer.copy(vertices, 0);

                regions[1].srcOffset = regions[0].size;
                regions[1].size = indexes.size() * sizeof(uint32_t);
                _stagingBuffer.copy(indexes, regions[1].srcOffset);

                _fence.reset();
                auto beginInfo = initializers::commandBufferBeginInfo();
                vkBeginCommandBuffer(commandBuffer, &beginInfo);

                assert(model->vertexBuffer.size >= regions[0].dstOffset);
                vkCmdCopyBuffer(commandBuffer, _stagingBuffer, model->vertexBuffer, 1, &regions[0]);

                assert(model->indexBuffer.size >= regions[1].dstOffset);
                vkCmdCopyBuffer(commandBuffer, _stagingBuffer, model->indexBuffer, 1, &regions[1]);

                vkEndCommandBuffer(commandBuffer);

                VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &commandBuffer;
                vkQueueSubmit(queue, 1, &submitInfo, _fence.fence);

                _fence.wait();

                Mesh mesh{
                        .meshId = static_cast<uint32_t>(meshId),
                        .indexCount = static_cast<uint32_t>(indexes.size()),
                        .instanceCount = 1u,
                        .firstIndex = firstIndex,
                        .vertexOffset = vertexOffset,
                        .firstInstance = 0,
                        .materialId = aiMesh->mMaterialIndex
                };
                model->meshes.push(mesh);

                firstIndex += mesh.indexCount;
                vertexOffset += vertices.size();
                regions[0].dstOffset += regions[0].size;
                regions[1].dstOffset += regions[1].size;
            }

            for(auto i = 0; i < scene->mNumMaterials; i++) {
                auto material = extractMaterial(scene->mMaterials[i]);
                material.textures = extractTextures(*scene->mMaterials[i], pending.model, pending.path);
                pending.model->materials.push_back(material);
            }

        }
    }


    void Loader::processVolumes() {
        while(!_pendingVolumes.empty()) {
            auto pending = _pendingVolumes.pop();
            auto volume = pending.volume;

            for(const auto& path : pending.paths) {
                auto bounds = volumeBounds(path);
                volume->bounds.min = glm::min(volume->bounds.min, bounds.min);
                volume->bounds.max = glm::max(volume->bounds.max, bounds.max);
            }

            glm::uvec3 dimensions{volume->bounds.max - volume->bounds.min};

            volume->updateTransform();

            auto size = dimensions.x * dimensions.y * dimensions.z * sizeof(float);
            if(size == 0){
                dimensions = glm::vec3(1);
                size = sizeof(float);
            }
            volume->staging =  _device->createStagingBuffer(size);
            textures::create(*_device, volume->texture, VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT,
                             dimensions, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float));
            volume->initialized = true;
            spdlog::info("volume bounds determined: [{}, {}]", volume->bounds.min, volume->bounds.max);
            for(const auto& path : pending.paths) {
                VolumeUploadRequest request{ path, volume };
                _pendingVolumeUploads.push(request);
            }
        }
    }

    glm::vec3 toGlm(openvdb::Vec3i v) {
        return {v.x(), v.y(), v.z()};
    }

    glm::vec3 toUVW(const glm::vec3& x, const Bounds& b){
        glm::vec3 d{b.max - b.min};
        d -= 1.0f;
        return  remap(x, b.min, b.max, glm::vec3(0), d);
    };

    void Loader::uploadVolumes() {
        while(!_pendingVolumeUploads.empty()) {
            auto pending = _pendingVolumeUploads.pop();
            auto path = pending.path;

            spdlog::debug("loading volume grid from {}", fs::path(path).filename().string());
            openvdb::io::File file(path.string());

            file.open();

            auto grid = openvdb::gridPtrCast<openvdb::FloatGrid>(file.readGrid(file.beginName().gridName()));
            openvdb::Vec3i boxMin = grid->getMetadata<openvdb::Vec3IMetadata>("file_bbox_min")->value();
            openvdb::Vec3i boxMax = grid->getMetadata<openvdb::Vec3IMetadata>("file_bbox_max")->value();
            int64_t numVoxels = grid->getMetadata<openvdb::Int64Metadata>("file_voxel_count")->value();

            VdbVolume volume{};
            if(numVoxels <= 0) {
                spdlog::info("{} contains no voxels", path.string());
                pending.volume->UploadedVolumes.push({path, volume});
                file.close();
                continue;
            }

            openvdb::Vec3i size;
            size = size.sub(boxMax, boxMin);

            openvdb::Coord xyz;
            auto accessor = grid->getAccessor();

            auto &z = xyz.z();
            auto &y = xyz.y();
            auto &x = xyz.x();

            static int count = 0;
            float maxDensity = MIN_FLOAT;
            volume.voxels.reserve(size.x() * size.y() * size.z());
            for (z = boxMin.z(); z <= boxMax.z(); z++) {
                for (y = boxMin.y(); y <= boxMax.y(); y++) {
                    for (x = boxMin.x(); x <= boxMax.x(); x++) {

                        auto value = accessor.getValue(xyz);
                        Voxel voxel{.position{xyz.x(), xyz.y(), xyz.z()}, .value = value};
                        voxel.position = toUVW(voxel.position, pending.volume->bounds);
                        maxDensity = glm::max(maxDensity, value);
                        volume.voxels.push_back(voxel);
                    }
                }
            }

            volume.bounds.min = toGlm(boxMin);
            volume.bounds.max = toGlm(boxMax);
            volume.invMaxDensity = maxDensity != 0 ? 1 / maxDensity : 0;
            file.close();

            pending.volume->UploadedVolumes.push({path, volume});
        }

    }

    Bounds Loader::volumeBounds(const std::filesystem::path &path) {
        spdlog::debug("loading volume grid from {}", fs::path(path).filename().string());
        std::ifstream fin{path.string(), std::ios::binary};
        openvdb::io::Stream gStream{fin};

        auto grid = *gStream.getGrids()->begin();
        int64_t numVoxels = grid->getMetadata<openvdb::Int64Metadata>("file_voxel_count")->value();

        openvdb::Vec3i boxMin = grid->getMetadata<openvdb::Vec3IMetadata>("file_bbox_min")->value();
        openvdb::Vec3i boxMax = grid->getMetadata<openvdb::Vec3IMetadata>("file_bbox_max")->value();

        return numVoxels == 0 ? Bounds{ glm::vec3(0), glm::vec3(0) } : Bounds{toGlm(boxMin), toGlm(boxMax) };
    }


    std::set<std::filesystem::path> Loader::extractTextures(const aiMaterial& material, const std::shared_ptr<Model>& model, const std::filesystem::path& path) {
        const auto rootPath = path.parent_path();

        aiString texPath;
        std::set<std::filesystem::path> textures;
        if(material.GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == aiReturn_SUCCESS) {
            TextureUploadRequest diffuse{ rootPath / texPath.C_Str(),  model, TextureType::DIFFUSE};
            textures.insert(diffuse.path);
            _pendingUploads.push(diffuse);
        }

        if(material.GetTexture(aiTextureType_AMBIENT, 0, &texPath) == aiReturn_SUCCESS) {
            TextureUploadRequest ambient{ rootPath / texPath.C_Str(), model, TextureType::AMBIENT };
            textures.insert(ambient.path);
            _pendingUploads.push(ambient);
        }

        if(material.GetTexture(aiTextureType_SPECULAR, 0, &texPath) == aiReturn_SUCCESS) {
            TextureUploadRequest specular{ rootPath / texPath.C_Str(), model, TextureType::SPECULAR };
            textures.insert(specular.path);
            _pendingUploads.push(specular);
        }

        if(material.GetTexture(aiTextureType_NORMALS, 0, &texPath) == aiReturn_SUCCESS
          || material.GetTexture(aiTextureType_HEIGHT, 0, &texPath) == aiReturn_SUCCESS){
            TextureUploadRequest normal{ rootPath / texPath.C_Str(), model, TextureType::NORMAL };
            textures.insert(normal.path);
            _pendingUploads.push(normal);
        }

        if(material.GetTexture(aiTextureType_OPACITY, 0, &texPath) == aiReturn_SUCCESS) {
            TextureUploadRequest opacity{ rootPath / texPath.C_Str(), model, TextureType::OPACITY };
            textures.insert(opacity.path);
            _pendingUploads.push(opacity);
        }

        if(material.GetTexture(aiTextureType_DISPLACEMENT, 0, &texPath) == aiReturn_SUCCESS) {
            TextureUploadRequest displacement{ rootPath / texPath.C_Str(), model, TextureType::DISPLACEMENT };
            textures.insert(displacement.path);
            _pendingUploads.push(displacement);
        }

        if(material.GetTexture(aiTextureType_SHININESS, 0, &texPath) == aiReturn_SUCCESS) {
            TextureUploadRequest shininess{ rootPath / texPath.C_Str(), model, TextureType::SHININESS };
            textures.insert(shininess.path);
            _pendingUploads.push(shininess);
        }
        return textures;
    }

    void Loader::uploadTextures() {
        while(!_pendingUploads.empty()) {
            auto request = _pendingUploads.pop();
            if(!_uploadedTextures.contains(request.path)) {
                createTexture(request);
                _uploadedTextures.insert(request.path);
            }
        }
    }

    void Loader::createTexture(TextureUploadRequest &request) {
        UploadedTexture tReady{ .path = request.path, .type = request.type };
        int texWidth, texHeight, texChannels;
        stbi_set_flip_vertically_on_load(1);
        stbi_uc* pixels = stbi_load(request.path.string().data(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        tReady.texture.width = texWidth;
        tReady.texture.height = texHeight;
        tReady.texture.depth = 1;
        tReady.texture.format = request.type == TextureType::DIFFUSE ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        if(!pixels){
            spdlog::warn("failed to load texture image {}!", request.path.string());
            return;
        }

        VkDeviceSize imageSize = texWidth * texHeight * 4;
        VulkanBuffer stagingBuffer = _device->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY
                                                           , imageSize, "", { *_device->queueFamilyIndex.transfer });
        stagingBuffer.copy(pixels, imageSize);

        VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        VkExtent3D extent{static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1 };

        tReady.texture.spec.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        tReady.texture.spec.imageType = VK_IMAGE_TYPE_2D;
        tReady.texture.spec.format = VK_FORMAT_R8G8B8A8_SRGB;
        tReady.texture.spec.extent = extent;
        tReady.texture.spec.mipLevels = 1;
        tReady.texture.spec.arrayLayers = 1;
        tReady.texture.spec.samples = VK_SAMPLE_COUNT_1_BIT;
        tReady.texture.spec.tiling = VK_IMAGE_TILING_OPTIMAL;
        tReady.texture.spec.usage = usageFlags;
        tReady.texture.spec.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        tReady.texture.spec.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        tReady.texture.image = _device->createImage(tReady.texture.spec);

        auto& commandPool = _device->commandPoolFor(*_device->queueFamilyIndex.transfer );

        commandPool.oneTimeCommand([&](auto commandBuffer) {
            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;

            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = extent;

            VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_HOST_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.image = tReady.texture.image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, tReady.texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = 0;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = *_device->queueFamilyIndex.transfer;
            barrier.dstQueueFamilyIndex = *_device->queueFamilyIndex.graphics;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_NONE, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        });

        request.model->uploadedTextures.push(std::move(tReady));
    }

    void Loader::stop() {
        _running = false;
        _loadRequest.notify_one();
        _thread.join();
    }

    Material Loader::extractMaterial(const aiMaterial* aiMaterial) {

        aiString name;
        auto ret = aiMaterial->Get(AI_MATKEY_NAME, name);
        Material material{ .name = name.C_Str()};

        float scalar = 1.0f;
        ret = aiMaterial->Get(AI_MATKEY_OPACITY, scalar);
        if(ret == aiReturn_SUCCESS){
            material.opacity = scalar;
        }

        ret = aiMaterial->Get(AI_MATKEY_REFRACTI, scalar);
        if(ret == aiReturn_SUCCESS){
            material.ior = scalar;
        }


        ret = aiMaterial->Get(AI_MATKEY_SHININESS, scalar);
        if(ret == aiReturn_SUCCESS){
            material.shininess = scalar;
        }
        ret = aiMaterial->Get(AI_MATKEY_SHADING_MODEL, scalar);
        if(ret == aiReturn_SUCCESS){
            material.illum = scalar;
        }

        glm::vec3 color;
        uint32_t size = 3;
        ret = aiMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, glm::value_ptr(color), &size);
        if(ret == aiReturn_SUCCESS){
            material.diffuse = color;
        }

        ret = aiMaterial->Get(AI_MATKEY_COLOR_AMBIENT, glm::value_ptr(color), &size);
        if(ret == aiReturn_SUCCESS){
            material.ambient = color;
        }

        ret = aiMaterial->Get(AI_MATKEY_COLOR_SPECULAR, glm::value_ptr(color), &size);
        if(ret == aiReturn_SUCCESS){
            material.specular = color;
        }

        ret = aiMaterial->Get(AI_MATKEY_COLOR_EMISSIVE, glm::value_ptr(color), &size);
        if(ret == aiReturn_SUCCESS){
            material.emission = color;
        }

        ret = aiMaterial->Get(AI_MATKEY_COLOR_TRANSPARENT, glm::value_ptr(color), &size);
        if(ret == aiReturn_SUCCESS){
            material.transmittance = color;
        }

        return material;
    }

    std::shared_ptr<Volume> Loader::loadVolume(const std::filesystem::path &path) {
        if(_pendingVolumes.full()){
            return {};
        }

        PendingVolume pending{ .volume = std::make_shared<Volume>() };
        auto& volume = pending.volume;
        volume->UploadedVolumes.reset(2048);

        if(std::filesystem::is_directory(path)) {
            for(const auto& entry : std::filesystem::directory_iterator{path}) {
                volume->numFrames++;
                pending.paths.push_back(entry);
            }
        }else {
            volume->numFrames = 1;
            pending.paths.push_back(path);
        }
        _pendingVolumes.push(pending);
        _loadRequest.notify_one();

        return volume;
    }

    void Model::updateDrawState(const VulkanDevice& device, BindlessDescriptor& bindlessDescriptor) {
        if(!meshes.empty()){
//            spdlog::info("{} meshes ready", meshes.size());
        }
        auto meshData = reinterpret_cast<MeshData*>(meshBuffer.map());
        auto materialData = reinterpret_cast<MaterialData*>(materialBuffer.map());

        for(int i = 0; i < materials.size(); i++){
            materialData[i].diffuse = materials[i].diffuse;
            materialData[i].ambient = materials[i].ambient;
            materialData[i].specular = materials[i].specular;
            materialData[i].emission = materials[i].emission;
            materialData[i].transmittance = materials[i].transmittance;
            materialData[i].shininess = materials[i].shininess;
            materialData[i].ior = materials[i].ior;
            materialData[i].opacity = materials[i].opacity;
            materialData[i].illum = materials[i].illum;
        }


        while(!meshes.empty()) {
            const auto mesh = meshes.pop();

            auto& drawCommand = draw.cpu[draw.count++];
            drawCommand.indexCount = mesh.indexCount;
            drawCommand.instanceCount = mesh.instanceCount;
            drawCommand.firstIndex = mesh.firstIndex;
            drawCommand.vertexOffset = mesh.vertexOffset;
            drawCommand.firstInstance = mesh.firstIndex;

            meshData[mesh.meshId].materialId = mesh.materialId;
        }

        auto numTextures = uploadedTextures.size();
        std::vector<VkDescriptorImageInfo> infos(numTextures);
        std::vector<VkWriteDescriptorSet> writes(numTextures, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET});

        for(auto i = 0; i < numTextures; i++) {
            auto uploaded = uploadedTextures.pop();
            auto& texture = uploaded.texture;
            VkImageSubresourceRange subResourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            texture.imageView = texture.image.createView(texture.spec.format, VK_IMAGE_VIEW_TYPE_2D, subResourceRange);
            uint32_t textureId = bindlessDescriptor.update(texture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            textures.push_back(std::move(texture));

            auto type = static_cast<int>(uploaded.type);

            for(int j = 0; j < materials.size(); ++j) {
                auto& material = materials[j];
                if(material.textures.contains(uploaded.path)){
                    if(type / 4 < 1) {
                        materialData[j].textures0[type] = textureId;
                    }else {
                        materialData[j].textures1[type%4] = textureId;
                    }
                }
            }
        }

        meshBuffer.unmap();
        materialBuffer.unmap();
    }

    void Volume::checkLoadState(BindlessDescriptor& bindlessDescriptor) {
        if(ready) return;

        static bool once = false; // FIXME use std::thread once flag
        if(initialized && !once) {
            once = true;
            bindlessDescriptor.update({ &texture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureBindingIndex});
        }

        static auto getId = [](const auto& path) {
            auto str = path.string();
            auto start = str.find_last_of('_') + 1;
            auto id = atoi(str.substr(start, 4).c_str());
            return id;
        };

        if(!UploadedVolumes.empty()){
            currentFrame = 0;
            spdlog::info("{} new frames available", UploadedVolumes.size());
        }
        while(!UploadedVolumes.empty()) {
            auto uploaded = UploadedVolumes.pop();
            Frame frame{ .volume = std::move(uploaded.volume), .index = getId(uploaded.path), .durationMS = framePeriod };
            frames.push_back(std::move(frame));
        }

        std::sort(frames.begin(), frames.end(), [](const auto& a, const auto& b) {
            return a.index < b.index;
        });
        for(auto& frame : frames){
            frame.elapsedMS = 0;
        }
        if(frames.size() == 1){
            nextFrame = true;
        }
        ready = numFrames != 0 && frames.size() == numFrames;
    }

    void Volume::update(float dt) {
        if(frames.empty() || frames.size() == 1) return;
        auto& frame = frames[currentFrame];

        static constexpr int ms = 1000;
        frame.elapsedMS += dt * ms;

        if(frame.elapsedMS >= frame.durationMS){
            frame.elapsedMS = 0;
            currentFrame++;
            currentFrame %= frames.size();
            nextFrame = true;
        }
    }

    void Volume::updateTransform() {
        Bounds sBounds {bounds.min * scale, bounds.max * scale};

        transform = glm::translate(glm::mat4{1}, offset);
        transform = glm::translate(transform, sBounds.min);
        transform = glm::scale(transform, sBounds.max - sBounds.min);
    }

    void Volume::advanceFrame(VulkanDevice& device) {
        if(!nextFrame) return;
        auto stagingArea = reinterpret_cast<float*>(staging.map());
        std::memset(stagingArea, 0, staging.size);

        const auto& frame = frames[currentFrame];

        const auto iSize = glm::ivec3(bounds.max - bounds.min);
        for(const auto& voxel : frame.volume.voxels) {
            auto vid = glm::ivec3(voxel.position);
            int loc = (vid.z * iSize.y + vid.y) * iSize.x + vid.x;
            stagingArea[loc] = voxel.value;
        }

        device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
            VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.image = texture.image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkBufferImageCopy region{ 0, 0, 0};
            region.imageOffset = {0, 0, 0};
            region.imageExtent = texture.spec.extent;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            vkCmdCopyBufferToImage(commandBuffer, staging.buffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        });

        nextFrame = false;
    }
}