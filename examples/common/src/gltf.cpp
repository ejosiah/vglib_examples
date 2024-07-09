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

glm::mat4 gltf::getTransformation(const tinygltf::Node& node) {
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

int alphaModeToIndex(const std::string& mode) {
    if(mode == "OPAQUE") return 0;
    if(mode == "MASK") return 1;
    if(mode == "BLEND") return 2;

    throw std::runtime_error{"unknown alpha mode"};
}

void gltf::PendingModel::updateDrawState() {

    auto drawOffset = model->draw_16.count.load();

    std::vector<VkDrawIndexedIndirectCommand> drawCommands16{};
    std::vector<VkDrawIndexedIndirectCommand> drawCommands32{};

    std::vector<MeshData> meshData16;
    std::vector<MeshData> meshData32;


    for(const auto id : gltf->scenes[0].nodes) {
        const auto& node = gltf->nodes[id];
        if(node.mesh != -1) {
            glm::mat4 pose = getTransformation(node);
            for(const auto cid : node.children) {
                const auto& childNode = gltf->nodes[cid];
                if(childNode.mesh != -1) {
                    const auto& childMesh = meshes[childNode.mesh];


                    VkDrawIndexedIndirectCommand drawCommand{};
                    drawCommand.indexCount = childMesh.indexCount;
                    drawCommand.instanceCount = 1;
                    drawCommand.firstIndex = childMesh.firstIndex;
                    drawCommand.vertexOffset = childMesh.vertexOffset;
                    drawCommand.firstInstance = 0;

                    glm::mat4 childPose = pose * getTransformation(childNode);

                    MeshData data{ .materialId = static_cast<int>(childMesh.materialId) };
                    data.model = childPose;
                    data.ModelInverse = glm::inverse(childPose);

                    if(childMesh.indexType == ComponentType::UNSIGNED_SHORT) {
                        drawCommands16.push_back(drawCommand);
                        meshData16.push_back(data);
                    }else {
                        drawCommands32.push_back(drawCommand);
                        meshData32.push_back(data);
                    }

                }
            }

            const auto& mesh = meshes[node.mesh];

            VkDrawIndexedIndirectCommand drawCommand{};
            drawCommand.indexCount = mesh.indexCount;
            drawCommand.instanceCount = 1;
            drawCommand.firstIndex = mesh.firstIndex;
            drawCommand.vertexOffset = mesh.vertexOffset;
            drawCommand.firstInstance = 0;

            MeshData data{ .materialId = static_cast<int>(mesh.materialId) };
            data.model = pose;
            data.ModelInverse = glm::inverse(pose);

            if(mesh.indexType == ComponentType::UNSIGNED_SHORT) {
                drawCommands16.push_back(drawCommand);
                meshData16.push_back(data);
            }else {
                drawCommands32.push_back(drawCommand);
                meshData32.push_back(data);
            }
        }
    }

    VulkanBuffer meshData16Staging = device->createStagingBuffer(BYTE_SIZE(meshData16) + sizeof(MeshData));
    VulkanBuffer meshData32Staging = device->createStagingBuffer(BYTE_SIZE(meshData32) + sizeof(MeshData));

    VulkanBuffer drawCmd16Staging = device->createStagingBuffer(BYTE_SIZE(drawCommands16) + sizeof(VkDrawIndexedIndirectCommand));
    VulkanBuffer drawCmd32Staging = device->createStagingBuffer(BYTE_SIZE(drawCommands32) + sizeof(VkDrawIndexedIndirectCommand));


    if(!meshData16.empty()) {
        meshData16Staging.copy(meshData16);
    }
    if(!meshData32.empty()) {
        meshData32Staging.copy(meshData32);
    }

    if(!drawCommands16.empty()) {
        drawCmd16Staging.copy(drawCommands16);
    }

    if(!drawCommands32.empty()) {
        drawCmd32Staging.copy(drawCommands32);
    }


    device->transferCommandPool().oneTimeCommand([&](auto commandBuffer){
        VkBufferCopy mesh16Region{};
        mesh16Region.srcOffset = 0;
        mesh16Region.dstOffset = 0;
        mesh16Region.size = meshData16Staging.size;

        VkBufferCopy mesh32Region{};
        mesh32Region.srcOffset = 0;
        mesh32Region.dstOffset = 0;
        mesh32Region.size = meshData32Staging.size;

        vkCmdCopyBuffer(commandBuffer, meshData16Staging.buffer, model->mesh16Buffer, 1, &mesh16Region);
        vkCmdCopyBuffer(commandBuffer, meshData32Staging.buffer, model->mesh32Buffer, 1, &mesh32Region);

        VkBufferCopy draw16Region{};
        draw16Region.srcOffset = 0;
        draw16Region.dstOffset = 0;
        draw16Region.size = drawCmd16Staging.size;

        VkBufferCopy draw32Region{};
        draw32Region.srcOffset = 0;
        draw32Region.dstOffset = 0;
        draw32Region.size = drawCmd32Staging.size;

        vkCmdCopyBuffer(commandBuffer, drawCmd16Staging.buffer, model->draw_16.gpu, 1, &draw16Region);
        vkCmdCopyBuffer(commandBuffer, drawCmd32Staging.buffer, model->draw_32.gpu, 1, &draw32Region);

        std::vector<VkBufferMemoryBarrier> barriers(4);
        barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barriers[0].srcQueueFamilyIndex = device->queueFamilyIndex.transfer.value();
        barriers[0].dstQueueFamilyIndex = device->queueFamilyIndex.graphics.value();
        barriers[0].offset = 0;
        barriers[0].buffer = model->mesh16Buffer;
        barriers[0].size = meshData16Staging.size;

        barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barriers[1].srcQueueFamilyIndex = device->queueFamilyIndex.transfer.value();
        barriers[1].dstQueueFamilyIndex = device->queueFamilyIndex.graphics.value();
        barriers[1].offset = 0;
        barriers[1].buffer = model->draw_16.gpu;
        barriers[1].size = drawCmd16Staging.size;

        barriers[2].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barriers[2].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barriers[2].srcQueueFamilyIndex = device->queueFamilyIndex.transfer.value();
        barriers[2].dstQueueFamilyIndex = device->queueFamilyIndex.graphics.value();
        barriers[2].offset = 0;
        barriers[2].buffer = model->mesh32Buffer;
        barriers[2].size = meshData32Staging.size;

        barriers[3].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barriers[3].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[3].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barriers[3].srcQueueFamilyIndex = device->queueFamilyIndex.transfer.value();
        barriers[3].dstQueueFamilyIndex = device->queueFamilyIndex.graphics.value();
        barriers[3].offset = 0;
        barriers[3].buffer = model->draw_32.gpu;
        barriers[3].size = drawCmd32Staging.size;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0,
                             VK_NULL_HANDLE, COUNT(barriers), barriers.data(), 0, VK_NULL_HANDLE);

    });

    model->draw_16.count += static_cast<int>(drawCommands16.size());
    model->draw_32.count += static_cast<int>(drawCommands32.size());

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
    while(draw_16.count >= numMeshes) {} //  TODO use condition variables
}

bool gltf::Model::fullyLoaded() const {
    return draw_16.count >= numMeshes && texturesLoaded();
}

bool gltf::Model::texturesLoaded() const {
    return textures.size() >= numTextures;
}
