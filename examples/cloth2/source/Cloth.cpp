#include "Cloth.hpp"
#include "primitives.h"
#include "VulkanInitializers.h"

Cloth::Cloth(VulkanDevice &device, std::vector<VkDescriptorSet> materialSets)
: _device(&device)
, _materialSets(materialSets)
{

}

void Cloth::init() {
    auto& device = *_device;

    auto state = initialState();

    _buffer = device.createDeviceLocalBuffer(state.vertices.data(), BYTE_SIZE(state.vertices), bufferUsage);
    _indices = device.createDeviceLocalBuffer(state.indices.data(), BYTE_SIZE(state.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    _indexCount = state.indices.size();
    _vertexCount = state.vertices.size();
    
    loadMaterial();

}

Vertices Cloth::initialState() const {
    float halfWidth = _size.x * 0.5f;
    glm::mat4 xform{1};
    xform = glm::translate(xform, {0, _size.x , 0});
    xform = glm::rotate(xform, -glm::half_pi<float>(), {1, 0, 0});
    return primitives::plane(_gridSize.x - 1, _gridSize.y - 1, _size.x, _size.y, xform, glm::vec4(0.4, 0.4, 0.4, 1.0));
}

void Cloth::bindVertexBuffers(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, _buffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, _indices, 0, VK_INDEX_TYPE_UINT32);
}

void Cloth::loadMaterial() {
    uint32_t levelCount  = 11;

    _materials.resize(3);
    std::vector<std::string> colors {
            R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\Fabric002_COL_1.jpg)",
            R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\Fabric002_COL_2.jpg)",
            R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\Fabric002_COL_3.jpg)"
    };
    textures::fromFile(*_device, _materials[0].albedo, colors, false, VK_FORMAT_R8G8B8A8_SRGB, levelCount);
    textures::fromFile(*_device, _materials[0].normal, R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\Fabric002_NRM.jpg)", false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);
    textures::fromFile(*_device, _materials[0].metalness, R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\Fabric002_METALNESS.jpg)", false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);
    textures::fromFile(*_device, _materials[0].roughness, R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\Fabric002_ROUGHNESS.jpg)", false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);
    textures::fromFile(*_device, _materials[0].ambientOcclusion, R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\Fabric002_AO.jpg)", false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);

    textures::generateLOD(*_device, _materials[0].albedo, levelCount, colors.size());
    textures::generateLOD(*_device, _materials[0].normal, levelCount);
    textures::generateLOD(*_device, _materials[0].roughness, levelCount);
    textures::generateLOD(*_device, _materials[0].ambientOcclusion, levelCount);
    _materials[0].descriptorSet = _materialSets[0];
    addMaterial(&_materials[0]);

    colors = { R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\Fabric001_COL.png)" };
    textures::fromFile(*_device, _materials[1].albedo, colors, false, VK_FORMAT_R8G8B8A8_SRGB, levelCount);
    textures::fromFile(*_device, _materials[1].normal, R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\Fabric001_NRM.png)", false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);
    textures::fromFile(*_device, _materials[1].metalness, R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\Fabric001_METALNESS.png)", false, VK_FORMAT_R8G8B8A8_UNORM);
    textures::fromFile(*_device, _materials[1].roughness, R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\Fabric001_ROUGHNESS.png)", false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);
    textures::fromFile(*_device, _materials[1].ambientOcclusion, R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\Fabric001_AO.png)", false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);

    textures::generateLOD(*_device, _materials[1].albedo, levelCount, colors.size());
    textures::generateLOD(*_device, _materials[1].normal, levelCount);
    textures::generateLOD(*_device, _materials[1].roughness, levelCount);
    textures::generateLOD(*_device, _materials[1].ambientOcclusion, levelCount);
    _materials[1].descriptorSet = _materialSets[1];
    addMaterial(&_materials[1]);

    colors = { R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\Fabric003_COL.png)" };
    textures::fromFile(*_device, _materials[2].albedo, colors, false, VK_FORMAT_R8G8B8A8_SRGB, levelCount);
    textures::fromFile(*_device, _materials[2].normal, R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\Fabric003_NRM.png)", false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);
    textures::fromFile(*_device, _materials[2].metalness, R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\Fabric003_METALNESS.png)", false, VK_FORMAT_R8G8B8A8_UNORM);
    textures::fromFile(*_device, _materials[2].roughness, R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\Fabric003_ROUGHNESS.png)", false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);
    textures::fromFile(*_device, _materials[2].ambientOcclusion, R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\Fabric003_AO.png)", false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);

    textures::generateLOD(*_device, _materials[2].albedo, levelCount, colors.size());
    textures::generateLOD(*_device, _materials[2].normal, levelCount);
    textures::generateLOD(*_device, _materials[2].roughness, levelCount);
    textures::generateLOD(*_device, _materials[2].ambientOcclusion, levelCount);
    _materials[2].descriptorSet = _materialSets[2];
    addMaterial(&_materials[2]);
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
