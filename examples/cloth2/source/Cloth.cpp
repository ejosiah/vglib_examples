#include "Cloth.hpp"
#include "primitives.h"
#include "VulkanInitializers.h"
#include "filemanager.hpp"

Cloth::Cloth(VulkanDevice &device, std::vector<VkDescriptorSet> materialSets, glm::vec2 numPoints, glm::vec2 size)
: _device(&device)
, _materialSets(materialSets)
, _gridSize(numPoints)
, _size(size)
, _numCells(numPoints - 1.f)
{
    if(_numCells.x % 2 == 1) _numCells.x += 1;
    if(_numCells.y % 2 == 1) _numCells.y += 1;
    _gridSize = glm::vec2(_numCells) + 1.f;
}

void Cloth::init() {
    auto& device = *_device;

    const auto state = initialState();
    const auto numPoints = state.vertices.size();
    auto invMasses = std::vector<float>(numPoints, 1);
    const auto width = static_cast<size_t>(_gridSize.x);

    invMasses[numPoints - width] = 0; // pin top left corner;
    invMasses[numPoints - 1] = 0; // pin top right corner;

    _buffer = device.createDeviceLocalBuffer(state.vertices.data(), BYTE_SIZE(state.vertices), bufferUsage);
    _restPositions = device.createDeviceLocalBuffer(state.vertices.data(), BYTE_SIZE(state.vertices), bufferUsage);
    _invMass = device.createDeviceLocalBuffer(invMasses.data(), BYTE_SIZE(invMasses), bufferUsage);
    _indices = device.createDeviceLocalBuffer(state.indices.data(), BYTE_SIZE(state.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    _indexCount = state.indices.size();
    _vertexCount = state.vertices.size();
    
    loadMaterial();

}

Vertices Cloth::initialState() const {
    glm::mat4 xform{1};
    xform = glm::translate(xform, {0, _size.x , 0});
    xform = glm::rotate(xform, -glm::half_pi<float>(), {1, 0, 0});

    return primitives::plane(_numCells.x, _numCells.y, _size.x, _size.y, xform, glm::vec4(0.4, 0.4, 0.4, 1.0));
}

void Cloth::bindVertexBuffers(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, _buffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, _indices, 0, VK_INDEX_TYPE_UINT32);
}

void Cloth::loadMaterial() {
    uint32_t levelCount  = 11;

    _materials.resize(4);
    std::vector<std::string> colors {
        FileManager::resource("FabricUpholsteryPolyesterChenilleComp001/COL_VAR1_2K.jpg"),
        FileManager::resource("FabricUpholsteryPolyesterChenilleComp001/COL_VAR2_2K.jpg"),
        FileManager::resource("FabricUpholsteryPolyesterChenilleComp001/COL_VAR3_2K.jpg"),
    };
    textures::fromFile(*_device, _materials[0].albedo, colors, false, VK_FORMAT_R8G8B8A8_SRGB, levelCount);
    textures::fromFile(*_device, _materials[0].normal, FileManager::resource("FabricUpholsteryPolyesterChenilleComp001/NRM_2K.jpg"), false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);
    textures::fromFile(*_device, _materials[0].metalness, FileManager::resource("FabricUpholsteryPolyesterChenilleComp001/METALNESS_2K.jpg"), false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);
    textures::fromFile(*_device, _materials[0].roughness, FileManager::resource("FabricUpholsteryPolyesterChenilleComp001/ROUGHNESS_2K.jpg"), false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);
    textures::fromFile(*_device, _materials[0].ambientOcclusion, FileManager::resource("FabricUpholsteryPolyesterChenilleComp001/AO_2K.jpg"), false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);

    textures::generateLOD(*_device, _materials[0].albedo, levelCount, colors.size());
    textures::generateLOD(*_device, _materials[0].normal, levelCount);
    textures::generateLOD(*_device, _materials[0].roughness, levelCount);
    textures::generateLOD(*_device, _materials[0].ambientOcclusion, levelCount);
    _materials[0].descriptorSet = _materialSets[0];
    addMaterial(&_materials[0]);

    colors = {
            FileManager::resource("FabricDenim005/COL_1K.png"),
            FileManager::resource("FabricDenim005/COL_1K.png")
    };
    textures::fromFile(*_device, _materials[1].albedo, colors, false, VK_FORMAT_R8G8B8A8_SRGB, levelCount);
    textures::fromFile(*_device, _materials[1].normal, FileManager::resource("FabricDenim005/NRM_1K.png"), false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);
    textures::fromFile(*_device, _materials[1].metalness, FileManager::resource("FabricDenim005/METALNESS_1K.png"), false, VK_FORMAT_R8G8B8A8_UNORM);
    textures::fromFile(*_device, _materials[1].roughness, FileManager::resource("FabricDenim005/ROUGHNESS_1K.png"), false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);
    textures::fromFile(*_device, _materials[1].ambientOcclusion, FileManager::resource("FabricDenim005/AO_1K.png"), false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);

    textures::generateLOD(*_device, _materials[1].albedo, levelCount, colors.size());
    textures::generateLOD(*_device, _materials[1].normal, levelCount);
    textures::generateLOD(*_device, _materials[1].roughness, levelCount);
    textures::generateLOD(*_device, _materials[1].ambientOcclusion, levelCount);
    _materials[1].descriptorSet = _materialSets[1];
    addMaterial(&_materials[1]);

    colors = {
            FileManager::resource("FabricBengalinePlaid001/COL_1K.png"),
            FileManager::resource("FabricBengalinePlaid001/COL_1K.png")
    };
    textures::fromFile(*_device, _materials[2].albedo, colors, false, VK_FORMAT_R8G8B8A8_SRGB, levelCount);
    textures::fromFile(*_device, _materials[2].normal, FileManager::resource("FabricBengalinePlaid001/NRM_1K.png"), false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);
    textures::fromFile(*_device, _materials[2].metalness, FileManager::resource("FabricBengalinePlaid001/METALNESS_1K.png"), false, VK_FORMAT_R8G8B8A8_UNORM);
    textures::fromFile(*_device, _materials[2].roughness, FileManager::resource("FabricBengalinePlaid001/ROUGHNESS_1K.png"), false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);
    textures::fromFile(*_device, _materials[2].ambientOcclusion, FileManager::resource("FabricBengalinePlaid001/AO_1K.png"), false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);

    textures::generateLOD(*_device, _materials[2].albedo, levelCount, colors.size());
    textures::generateLOD(*_device, _materials[2].normal, levelCount);
    textures::generateLOD(*_device, _materials[2].roughness, levelCount);
    textures::generateLOD(*_device, _materials[2].ambientOcclusion, levelCount);
    _materials[2].descriptorSet = _materialSets[2];
    addMaterial(&_materials[2]);

    colors = {
            FileManager::resource("FabricWovenStriped001/COL_2K.png"),
            FileManager::resource("FabricWovenStriped001/COL_2K.png")
            };
    textures::fromFile(*_device, _materials[3].albedo, colors, false, VK_FORMAT_R8G8B8A8_SRGB, levelCount);
    textures::fromFile(*_device, _materials[3].normal, FileManager::resource("FabricWovenStriped001/NRM_2K.png"), false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);
    textures::fromFile(*_device, _materials[3].metalness, FileManager::resource("FabricWovenStriped001/METALNESS_2K.png"), false, VK_FORMAT_R8G8B8A8_UNORM);
    textures::fromFile(*_device, _materials[3].roughness, FileManager::resource("FabricWovenStriped001/ROUGHNESS_2K.png"), false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);
    textures::fromFile(*_device, _materials[3].ambientOcclusion, FileManager::resource("FabricWovenStriped001/AO_2K.png"), false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);

    textures::generateLOD(*_device, _materials[3].albedo, levelCount, colors.size());
    textures::generateLOD(*_device, _materials[3].normal, levelCount);
    textures::generateLOD(*_device, _materials[3].roughness, levelCount);
    textures::generateLOD(*_device, _materials[3].ambientOcclusion, levelCount);
    _materials[3].descriptorSet = _materialSets[3];
    addMaterial(&_materials[3]);
}

void Cloth::addMaterial(Material *material) {
    auto writes = initializers::writeDescriptorSets<5>();

    writes[0].dstSet = material->descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo albedoInfo{ material->albedo.sampler.handle, material->albedo.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[0].pImageInfo = &albedoInfo;

    writes[1].dstSet = material->descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo normalInfo{ material->normal.sampler.handle, material->normal.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[1].pImageInfo = &normalInfo;

    writes[2].dstSet = material->descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo metalInfo{ material->metalness.sampler.handle, material->metalness.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[2].pImageInfo = &metalInfo;

    writes[3].dstSet = material->descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo roughnessInfo{ material->roughness.sampler.handle, material->roughness.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[3].pImageInfo = &roughnessInfo;

    writes[4].dstSet = material->descriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].descriptorCount = 1;
    VkDescriptorImageInfo aoInfo{ material->ambientOcclusion.sampler.handle, material->ambientOcclusion.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[4].pImageInfo = &aoInfo;

    _device->updateDescriptorSets(writes);
}

void Cloth::bindMaterial(VkCommandBuffer commandBuffer, VkPipelineLayout layout, int id) {
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &_materials[id].descriptorSet, 0, VK_NULL_HANDLE);
}

size_t Cloth::numMaterials() const {
    return _materials.size();
}
