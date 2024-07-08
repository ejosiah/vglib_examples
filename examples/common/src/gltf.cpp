#include "gltf/gltf.hpp"

glm::vec2 gltf::vec2From(const std::vector<double>& v) {
    glm::vec2 res{};

    if(v.size() >= 1) res.x = v[0];
    if(v.size() >= 2) res.y = v[1];

    return res;
}

glm::vec3 gltf::vec3From(const std::vector<double>& v) {
    glm::vec3 res{};

    if(v.size() >= 1) res.x = v[0];
    if(v.size() >= 2) res.y = v[1];
    if(v.size() >= 3) res.z = v[2];

    return res;
}

glm::vec4 gltf::vec4From(const std::vector<double>& v) {
    glm::vec4 res{};

    if(v.size() >= 1) res.x = v[0];
    if(v.size() >= 2) res.y = v[1];
    if(v.size() >= 3) res.z = v[2];
    if(v.size() >= 4) res.w = v[3];

    return res;
}

glm::quat gltf::quaternionFrom(const std::vector<double> &q) {
    glm::quat res{};
    if(q.empty()) return res;

    res.x = q[0];
    res.y = q[1];
    res.z = q[2];
    res.w = q[3];
}

int alphaModeToIndex(const std::string& mode) {
    if(mode == "OPAQUE") return 0;
    if(mode == "MASK") return 1;
    if(mode == "BLEND") return 2;

    throw std::runtime_error{"unknown alpha mode"};
}

void gltf::PendingModel::updateDrawState() {

    auto drawOffset = model->draw.count.load();
    auto numMeshes = meshes.size();

    std::vector<VkDrawIndexedIndirectCommand> drawCommands{};
    std::vector<MeshData> meshData;
    while(!meshes.empty()) {
        const auto mesh = meshes.pop();

        VkDrawIndexedIndirectCommand drawCommand{};
        drawCommand.indexCount = mesh.indexCount;
        drawCommand.instanceCount = mesh.instanceCount;
        drawCommand.firstIndex = mesh.firstIndex;
        drawCommand.vertexOffset = mesh.vertexOffset;
        drawCommand.firstInstance = 0;
        drawCommands.push_back(drawCommand);

        MeshData data{ .materialId = static_cast<int>(mesh.materialId) };
        meshData.push_back(data);
    }

    if(numMeshes == 0) return;
    
    auto meshDataSize = BYTE_SIZE(meshData);
    auto drawCommandsSize = BYTE_SIZE(drawCommands);
    auto size =  meshDataSize + drawCommandsSize;
    VulkanBuffer stagingBuffer = device->createStagingBuffer(size);

    auto meshDataView = stagingBuffer.region(0, meshDataSize);
    auto drawCmdView = stagingBuffer.region(meshDataSize, meshDataSize + drawCommandsSize);

    meshDataView.upload(meshData.data());
    drawCmdView.upload(drawCommands.data());

    device->transferCommandPool().oneTimeCommand([&](auto commandBuffer){
        VkBufferCopy region{};
        region.srcOffset = meshDataView.offset;
        region.dstOffset = drawOffset * sizeof(MeshData);
        region.size = meshDataView.size();

        vkCmdCopyBuffer(commandBuffer, *meshDataView.buffer, model->meshBuffer, 1, &region);

        region.srcOffset = drawCmdView.offset;
        region.dstOffset = drawOffset * sizeof(VkDrawIndexedIndirectCommand);
        region.size = drawCmdView.size();

        vkCmdCopyBuffer(commandBuffer, *drawCmdView.buffer, model->draw.gpu, 1, &region);

        std::vector<VkBufferMemoryBarrier> barriers(2);
        barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barriers[0].srcQueueFamilyIndex = device->queueFamilyIndex.transfer.value();
        barriers[0].dstQueueFamilyIndex = device->queueFamilyIndex.graphics.value();
        barriers[0].offset = drawOffset * sizeof(MeshData);
        barriers[0].buffer = model->meshBuffer;
        barriers[0].size = meshDataView.size();

        barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barriers[1].srcQueueFamilyIndex = device->queueFamilyIndex.transfer.value();
        barriers[1].dstQueueFamilyIndex = device->queueFamilyIndex.graphics.value();
        barriers[1].offset = drawOffset * sizeof(VkDrawIndexedIndirectCommand);
        barriers[1].buffer = model->draw.gpu;
        barriers[1].size = drawCmdView.size();

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0,
                             VK_NULL_HANDLE, COUNT(barriers), barriers.data(), 0, VK_NULL_HANDLE);

    });

    model->draw.count += static_cast<int>(numMeshes);

}

void gltf::PendingModel::uploadMaterials() {
    std::vector<MaterialData> materials{};
    materials.reserve(gltf->materials.size());

    for(const auto& gltfMaterial : gltf->materials) {
        MaterialData material{};
        material.baseColor = vec4From(gltfMaterial.pbrMetallicRoughness.baseColorFactor);
        material.roughness = gltfMaterial.pbrMetallicRoughness.roughnessFactor;
        material.metalness = gltfMaterial.pbrMetallicRoughness.metallicFactor;
        material.emission = vec3From(gltfMaterial.emissiveFactor);
        material.alphaMode = alphaModeToIndex(gltfMaterial.alphaMode);
        material.alphaCutOff = gltfMaterial.alphaCutoff;
        material.doubleSided = gltfMaterial.doubleSided;

        if(gltfMaterial.pbrMetallicRoughness.baseColorTexture.index != -1) {
            material.textures[indexOf(TextureType::BASE_COLOR)] = gltfMaterial.pbrMetallicRoughness.baseColorTexture.index + textureBindingOffset;
        }

        if(gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index != -1) {
            material.textures[indexOf(TextureType::METALLIC_ROUGHNESS)] = gltfMaterial.pbrMetallicRoughness.metallicRoughnessTexture.index + textureBindingOffset;
        }

        if(gltfMaterial.normalTexture.index != -1 ) {
            material.textures[indexOf(TextureType::NORMAL)] = gltfMaterial.normalTexture.index + textureBindingOffset;
        }

        if(gltfMaterial.emissiveTexture.index != -1) {
            material.textures[indexOf(TextureType::EMISSION)] = gltfMaterial.emissiveTexture.index + textureBindingOffset;
        }

        if(gltfMaterial.occlusionTexture.index != -1) {
            material.textures[indexOf(TextureType::OCCLUSION)] = gltfMaterial.occlusionTexture.index + textureBindingOffset;
        }

        materials.push_back(material);
    }

    VulkanBuffer stagingBuffer = device->createStagingBuffer(BYTE_SIZE(materials));
    stagingBuffer.copy(materials);

    device->transferCommandPool().oneTimeCommand([&](auto commandBuffer) {
        VkBufferCopy region{};
        region.srcOffset = 0;
        region.dstOffset = 0;   // uploading all materials at once so no need for offset
        region.size = stagingBuffer.size;

        vkCmdCopyBuffer(commandBuffer, stagingBuffer, model->materialBuffer, 1, &region);

        VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.srcQueueFamilyIndex = device->queueFamilyIndex.transfer.value();
        barrier.dstQueueFamilyIndex = device->queueFamilyIndex.graphics.value();
        barrier.offset = 0;
        barrier.buffer = model->materialBuffer;
        barrier.size = model->materialBuffer.size;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0,
                             VK_NULL_HANDLE, 1, &barrier, 0, VK_NULL_HANDLE);
    });
}

void gltf::Model::sync() const {
    while( draw.count >= numMeshes) {} //  TODO use condition variables
}

bool gltf::Model::fullyLoaded() const {
    return draw.count >= numMeshes && texturesLoaded();
}

bool gltf::Model::texturesLoaded() const {
    return textures.size() >= numTextures;
}
