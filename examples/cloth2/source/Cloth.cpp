#include "Cloth.hpp"
#include "primitives.h"
#include "VulkanInitializers.h"

Cloth::Cloth(VulkanDevice &device, VkDescriptorSet materialSet)
: _device(&device)
{
    _material.descriptorSet = materialSet;
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
    textures::fromFile(*_device, _material.albedo, R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\FabricDenim_COL.png)", false, VK_FORMAT_R8G8B8A8_SRGB, levelCount);
    textures::fromFile(*_device, _material.normal, R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\FabricDenim_NRM.png)", false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);
    textures::fromFile(*_device, _material.metalness, R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\FabricDenim_METALNESS.png)", false, VK_FORMAT_R8G8B8A8_UNORM);
    textures::fromFile(*_device, _material.roughness, R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\FabricDenim_ROUGHNESS.png)", false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);
    textures::fromFile(*_device, _material.ambientOcclusion, R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\cloth2\data\FabricDenim_AO.png)", false, VK_FORMAT_R8G8B8A8_UNORM, levelCount);


    textures::generateLOD(*_device, _material.albedo, levelCount);
    textures::generateLOD(*_device, _material.normal, levelCount);
    textures::generateLOD(*_device, _material.roughness, levelCount);
    textures::generateLOD(*_device, _material.ambientOcclusion, levelCount);

    auto writes = initializers::writeDescriptorSets<5>();
    
    writes[0].dstSet = _material.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo albedoInfo{ _material.albedo.sampler.handle, _material.albedo.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[0].pImageInfo = &albedoInfo;

    writes[1].dstSet = _material.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo normalInfo{ _material.normal.sampler.handle, _material.normal.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[1].pImageInfo = &normalInfo;

    writes[2].dstSet = _material.descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo metalInfo{ _material.metalness.sampler.handle, _material.metalness.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[2].pImageInfo = &metalInfo;

    writes[3].dstSet = _material.descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo roughnessInfo{ _material.roughness.sampler.handle, _material.roughness.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[3].pImageInfo = &roughnessInfo;

    writes[4].dstSet = _material.descriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].descriptorCount = 1;
    VkDescriptorImageInfo aoInfo{ _material.ambientOcclusion.sampler.handle, _material.ambientOcclusion.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[4].pImageInfo = &aoInfo;

    _device->updateDescriptorSets(writes);
}

void Cloth::bindMaterial(VkCommandBuffer commandBuffer, VkPipelineLayout layout) {
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &_material.descriptorSet, 0, VK_NULL_HANDLE);
}