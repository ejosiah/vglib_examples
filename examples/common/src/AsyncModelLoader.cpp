#include "AsyncModelLoader.hpp"

#include <spdlog/spdlog.h>
#include <assimp/postprocess.h>

#include <functional>

namespace asyncml {

    constexpr uint32_t DEFAULT_PROCESS_FLAGS = aiProcess_GenSmoothNormals | aiProcess_Triangulate
                                               | aiProcess_CalcTangentSpace | aiProcess_JoinIdenticalVertices
                                               | aiProcess_ValidateDataStructure;

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
    , _pending{reserve}
    , _pendingUploads(2048)
    {}

    void Loader::start() {
        _stagingBuffer = _device->createStagingBuffer(_64MB, { *_device->queueFamilyIndex.transfer });
        _fence = _device->createFence();
        _running = true;
        _thread = std::thread{[this] { execLoop(); }};
        spdlog::info("model loader started");
    }

    std::shared_ptr<Model> asyncml::Loader::load(const std::filesystem::path &path, float unit) {
        spdlog::info("Mesh size: {}", sizeof(MeshData));
        if(_pending.full()) {
            return {};
        }
        const auto queueFamilyIndex = *_device->queueFamilyIndex.transfer;

        Pending pending{.model = std::make_shared<Model>(), .scene = _importer.ReadFile(path.string(), DEFAULT_PROCESS_FLAGS), .path = path, .unit = unit};
        const auto numMeshes = pending.scene->mNumMeshes;
        pending.model->meshes.reset(numMeshes);
        pending.model->materials.reset(2048);
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

        _pending.push(pending);

        spdlog::info("loading model from {}, containing {} meshes with {} vertices", path.string(), numMeshes, numVertices);

        return pending.model;
    }

    void Loader::execLoop() {
        while(_running) {
            using namespace std::chrono_literals;
            if(_pending.empty() && _pendingUploads.empty()) {
                std::this_thread::sleep_for(100ms);
                continue;
            }
            uploadMeshes();
            uploadTextures();
        }
        spdlog::info("async loader offline");
    }

    void Loader::uploadMeshes() {
        const auto queue = _device->queues.transfer;
        const auto& commandPool = _device->createCommandPool(*_device->queueFamilyIndex.transfer, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
        auto commandBuffer = commandPool.allocate();

        while(!_pending.empty()) {
            auto pending = _pending.pop();
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

                extractTextures(pending, aiMesh, meshId);

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

                regions[1].srcOffset += regions[0].size;
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
                        .firstInstance = 0
                };
                extractMaterial(pending.scene->mMaterials[aiMesh->mMaterialIndex], mesh);
                model->meshes.push(mesh);

                firstIndex += mesh.indexCount;
                vertexOffset += vertices.size();
                regions[0].dstOffset += regions[0].size;
                regions[1].dstOffset += regions[1].size;
            }
        }
    }


    void Loader::extractTextures(const Pending& pending, const aiMesh* mesh, uint32_t meshId) {
        const auto rootPath = pending.path.parent_path();

        auto material = pending.scene->mMaterials[mesh->mMaterialIndex];
        aiString texPath;

        if(material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == aiReturn_SUCCESS) {
            TextureUploadRequest diffuse{ rootPath / texPath.C_Str(), pending.model, 0, meshId };
            _pendingUploads.push(diffuse);
        }

        if(material->GetTexture(aiTextureType_AMBIENT, 0, &texPath) == aiReturn_SUCCESS) {
            TextureUploadRequest ambient{ rootPath / texPath.C_Str(), pending.model, 1, meshId };
            _pendingUploads.push(ambient);
        }

        if(material->GetTexture(aiTextureType_SPECULAR, 0, &texPath) == aiReturn_SUCCESS) {
            TextureUploadRequest specular{ rootPath / texPath.C_Str(), pending.model, 2, meshId };
            _pendingUploads.push(specular);
        }

        if(material->GetTexture(aiTextureType_NORMALS, 0, &texPath) == aiReturn_SUCCESS){
            TextureUploadRequest normal{ rootPath / texPath.C_Str(), pending.model, 3, meshId };
            _pendingUploads.push(normal);
        }else if(material->GetTexture(aiTextureType_HEIGHT, 0, &texPath) == aiReturn_SUCCESS){
            TextureUploadRequest normal{ rootPath / texPath.C_Str(), pending.model, 3, meshId };
            _pendingUploads.push(normal);
        }

        if(material->GetTexture(aiTextureType_OPACITY, 0, &texPath) == aiReturn_SUCCESS) {
            TextureUploadRequest opacity{ rootPath / texPath.C_Str(), pending.model, 4, meshId };
            _pendingUploads.push(opacity);
        }

        if(material->GetTexture(aiTextureType_DISPLACEMENT, 0, &texPath) == aiReturn_SUCCESS) {
            TextureUploadRequest displacement{ rootPath / texPath.C_Str(), pending.model, 5, meshId };
            _pendingUploads.push(displacement);
        }

        if(material->GetTexture(aiTextureType_SHININESS, 0, &texPath) == aiReturn_SUCCESS) {
            TextureUploadRequest shininess{ rootPath / texPath.C_Str(), pending.model, 6, meshId };
            _pendingUploads.push(shininess);
        }


//        if(material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == aiReturn_SUCCESS){
//            spdlog::info("aiTextureType_DIFFUSE: {}", texPath.C_Str());
//        }
//        if(material->GetTexture(aiTextureType_SPECULAR, 0, &texPath) == aiReturn_SUCCESS){
//            spdlog::info("aiTextureType_SPECULAR: {}", texPath.C_Str());
//        }
//        if(material->GetTexture(aiTextureType_AMBIENT, 0, &texPath) == aiReturn_SUCCESS){
//            spdlog::info("aiTextureType_AMBIENT: {}", texPath.C_Str());
//        }
//        if(material->GetTexture(aiTextureType_EMISSIVE, 0, &texPath) == aiReturn_SUCCESS){
//            spdlog::info("aiTextureType_EMISSIVE: {}", texPath.C_Str());
//        }
//        if(material->GetTexture(aiTextureType_HEIGHT, 0, &texPath) == aiReturn_SUCCESS){
//            spdlog::info("aiTextureType_HEIGHT: {}", texPath.C_Str());
//        }
//        if(material->GetTexture(aiTextureType_NORMALS, 0, &texPath) == aiReturn_SUCCESS){
//            spdlog::info("aiTextureType_NORMALS: {}", texPath.C_Str());
//        }
//
//        if(material->GetTexture(aiTextureType_SHININESS, 0, &texPath) == aiReturn_SUCCESS){
//            spdlog::info("aiTextureType_SHININESS: {}", texPath.C_Str());
//        }
//        if(material->GetTexture(aiTextureType_OPACITY, 0, &texPath) == aiReturn_SUCCESS){
//            spdlog::info("aiTextureType_OPACITY: {}", texPath.C_Str());
//        }
//        if(material->GetTexture(aiTextureType_DISPLACEMENT, 0, &texPath) == aiReturn_SUCCESS){
//            spdlog::info("aiTextureType_DISPLACEMENT: {}", texPath.C_Str());
//        }
//        if(material->GetTexture(aiTextureType_LIGHTMAP, 0, &texPath) == aiReturn_SUCCESS){
//            spdlog::info("aiTextureType_LIGHTMAP: {}", texPath.C_Str());
//        }
//
//        if(material->GetTexture(aiTextureType_REFLECTION, 0, &texPath) == aiReturn_SUCCESS){
//            spdlog::info("aiTextureType_REFLECTION: {}", texPath.C_Str());
//        }
//
//        if(material->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) == aiReturn_SUCCESS){
//            spdlog::info("aiTextureType_BASE_COLOR: {}", texPath.C_Str());
//        }
//        if(material->GetTexture(aiTextureType_NORMAL_CAMERA, 0, &texPath) == aiReturn_SUCCESS){
//            spdlog::info("aiTextureType_NORMAL_CAMERA: {}", texPath.C_Str());
//        }
//        if(material->GetTexture(aiTextureType_EMISSION_COLOR, 0, &texPath) == aiReturn_SUCCESS){
//            spdlog::info("aiTextureType_EMISSION_COLOR: {}", texPath.C_Str());
//        }
//        if(material->GetTexture(aiTextureType_METALNESS, 0, &texPath) == aiReturn_SUCCESS){
//            spdlog::info("aiTextureType_METALNESS: {}", texPath.C_Str());
//        }
//        if(material->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &texPath) == aiReturn_SUCCESS){
//            spdlog::info("aiTextureType_DIFFUSE_ROUGHNESS: {}", texPath.C_Str());
//        }
//        if(material->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &texPath) == aiReturn_SUCCESS){
//            spdlog::info("aiTextureType_AMBIENT_OCCLUSION: {}", texPath.C_Str());
//        }

    }

    void Loader::uploadTextures() {
        while(!_pendingUploads.empty()) {
            auto request = _pendingUploads.pop();
            createTexture(request);
        }
    }

    void Loader::createTexture(const TextureUploadRequest &request) {
        Material tReady{ .id = request.id, .meshId = request.meshId };
        int texWidth, texHeight, texChannels;
        stbi_set_flip_vertically_on_load(1);
        stbi_uc* pixels = stbi_load(request.path.string().data(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        tReady.texture.width = texWidth;
        tReady.texture.height = texHeight;
        tReady.texture.depth = 1;
        tReady.texture.format = request.id == 0 ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
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

        request.model->materials.push(std::move(tReady));
    }

    void Loader::stop() {
        _running = false;
        _thread.join();
    }

    void Loader::extractMaterial(const aiMaterial* aiMaterial, Mesh &mesh) {

        float scalar = 1.0f;
        auto ret = aiMaterial->Get(AI_MATKEY_OPACITY, scalar);
        if(ret == aiReturn_SUCCESS){
            mesh.opacity = scalar;
        }

        ret = aiMaterial->Get(AI_MATKEY_REFRACTI, scalar);
        if(ret == aiReturn_SUCCESS){
            mesh.ior = scalar;
        }


        ret = aiMaterial->Get(AI_MATKEY_SHININESS, scalar);
        if(ret == aiReturn_SUCCESS){
            mesh.shininess = scalar;
        }
        ret = aiMaterial->Get(AI_MATKEY_SHADING_MODEL, scalar);
        if(ret == aiReturn_SUCCESS){
            mesh.illum = scalar;
        }

        glm::vec3 color;
        uint32_t size = 3;
        ret = aiMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, glm::value_ptr(color), &size);
        if(ret == aiReturn_SUCCESS){
            mesh.diffuse = color;
        }

        ret = aiMaterial->Get(AI_MATKEY_COLOR_AMBIENT, glm::value_ptr(color), &size);
        if(ret == aiReturn_SUCCESS){
            mesh.ambient = color;
        }

        ret = aiMaterial->Get(AI_MATKEY_COLOR_SPECULAR, glm::value_ptr(color), &size);
        if(ret == aiReturn_SUCCESS){
            mesh.specular = color;
        }

        ret = aiMaterial->Get(AI_MATKEY_COLOR_EMISSIVE, glm::value_ptr(color), &size);
        if(ret == aiReturn_SUCCESS){
            mesh.emission = color;
        }

        ret = aiMaterial->Get(AI_MATKEY_COLOR_TRANSPARENT, glm::value_ptr(color), &size);
        if(ret == aiReturn_SUCCESS){
            mesh.transmittance = color;
        }
    }

    void Model::updateDrawState(const VulkanDevice& device, VkDescriptorSet bindlessDescriptor) {
        if(!meshes.empty()){
//            spdlog::info("{} meshes ready", meshes.size());
        }

        auto meshData = reinterpret_cast<MeshData*>(meshBuffer.map());

        while(!meshes.empty()) {
            const auto mesh = meshes.pop();
            meshDrawIds.insert(std::make_pair(mesh.meshId, draw.count));

            auto& drawCommand = draw.cpu[draw.count++];
            drawCommand.indexCount = mesh.indexCount;
            drawCommand.instanceCount = mesh.instanceCount;
            drawCommand.firstIndex = mesh.firstIndex;
            drawCommand.vertexOffset = mesh.vertexOffset;
            drawCommand.firstInstance = mesh.firstIndex;

            meshData[mesh.meshId].diffuse = mesh.diffuse;
            meshData[mesh.meshId].ambient = mesh.ambient;
            meshData[mesh.meshId].specular = mesh.specular;
            meshData[mesh.meshId].emission = mesh.emission;
            meshData[mesh.meshId].transmittance = mesh.transmittance;
            meshData[mesh.meshId].transmittance = mesh.transmittance;
            meshData[mesh.meshId].shininess = mesh.shininess;
            meshData[mesh.meshId].ior = mesh.ior;
            meshData[mesh.meshId].opacity = mesh.opacity;
            meshData[mesh.meshId].illum = mesh.illum;
        }

        // TODO allocate texture index from descriptor
        const auto numMaterials = materials.size();
        std::vector<VkDescriptorImageInfo> infos(numMaterials);
        std::vector<VkWriteDescriptorSet> writes(numMaterials, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET});

        for(auto i = 0; i < numMaterials; i++) {
            auto material = materials.pop();
            auto& texture = material.texture;
            auto drawId = meshDrawIds[material.meshId];   // TODO mesh might not be ready, return material in such a case
            if(material.id / 4 < 1) {
                meshData[drawId].textures0[material.id] = textures.size() + 1; // save slot zero for dummy texture
            }else {
                meshData[drawId].textures1[material.id % 4] = textures.size() + 1; // save slot zero for dummy texture
            }

            VkImageSubresourceRange subResourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            texture.imageView = texture.image.createView(texture.spec.format, VK_IMAGE_VIEW_TYPE_2D, subResourceRange);
            infos[i] = {VK_NULL_HANDLE, texture.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

            writes[i].dstSet = bindlessDescriptor;
            writes[i].dstBinding = 10;
            writes[i].dstArrayElement = textures.size() + 1; // save slot zero for dummy texture
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &infos[i];

            textures.push_back(std::move(texture));
        }

        device.updateDescriptorSets(writes);
    }
}