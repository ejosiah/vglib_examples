#pragma once

#include "VulkanDevice.h"
#include "Vertex.h"
#include "Texture.h"
#include <glm/glm.hpp>


class Cloth {
    struct Material{
        Texture albedo;
        Texture normal;
        Texture metalness;
        Texture roughness;
        Texture ambientOcclusion;
        VkDescriptorSet descriptorSet{};
    };

public:
    enum class Attributes : int { Vertex = 0, Normal, Count};
    enum class AttributeSize : VkDeviceSize { Vertex = sizeof(glm::vec4), Normal = sizeof(glm::vec4) };

    Cloth() = default;

    Cloth(VulkanDevice& device, std::vector<VkDescriptorSet> materialSets);

    void init();

    void loadMaterial();

    void addMaterial(Material* material);

    inline uint32_t indexCount() const { return _indexCount; }

    inline uint32_t vertexCount() const { return _vertexCount; }

    inline glm::vec2 size() const { return _size; }

    inline glm::vec2 gridSize() const { return _gridSize; }

    inline VulkanBuffer buffer() const { return _buffer; }

    void bindVertexBuffers(VkCommandBuffer commandBuffer);

    void bindMaterial(VkCommandBuffer commandBuffer, VkPipelineLayout layout, int id = 0);

    Vertices initialState() const;

    size_t numMaterials() const;

private:
    VulkanDevice* _device{};
    VulkanBuffer _buffer;
    VulkanBuffer _indices;
    uint32_t _indexCount{};
    uint32_t _vertexCount{};
    glm::vec2 _size = glm::vec2(6);
    glm::vec2 _gridSize{100};
    VkBufferUsageFlags bufferUsage =
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    std::vector<Material> _materials;
    std::vector<VkDescriptorSet> _materialSets;
};