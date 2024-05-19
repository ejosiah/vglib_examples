#include "Cloth.hpp"
#include "primitives.h"

Cloth::Cloth(VulkanDevice &device)
: _device(&device)
{}

void Cloth::init() {
    auto& device = *_device;

    auto state = initialState();

    _buffer = device.createDeviceLocalBuffer(state.vertices.data(), BYTE_SIZE(state.vertices), bufferUsage);
    _indices = device.createDeviceLocalBuffer(state.indices.data(), BYTE_SIZE(state.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    _indexCount = state.indices.size();
    _vertexCount = state.vertices.size();

}

Vertices Cloth::initialState() {
    float halfWidth = _size.x * 0.5f;
    glm::mat4 xform{1};
    xform = glm::translate(xform, {0, _size.x , halfWidth});
    xform = glm::rotate(xform, -glm::half_pi<float>(), {1, 0, 0});
    return primitives::plane(_gridSize.x - 1, _gridSize.y - 1, _size.x, _size.y, xform, glm::vec4(0.4, 0.4, 0.4, 1.0));
}

void Cloth::bindVertexBuffers(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, _buffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, _indices, 0, VK_INDEX_TYPE_UINT32);
}