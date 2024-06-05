#pragma once

#include "VulkanDevice.h"
#include "ComputePipelins.hpp"
#include "Voxel.hpp"
#include "Vertex.h"

class Marcher final : public ComputePipelines {
public:
    struct Mesh {
        VulkanBuffer vertices;
        VulkanBuffer indices;
        VulkanBuffer drawCmd;
        VulkanBuffer vertexCount;
        uint32_t* numVertices{};
    };

    Marcher() = default;

    Marcher(Voxels* voxels, float minGridSize);

    void init();

    Mesh generateMesh(float gridSize);

    void generateMesh(VkCommandBuffer commandBuffer, float gridSize);

    std::tuple<std::vector<uint32_t>, std::vector<Vertex>> generateIndices(std::vector<Vertex> vertices, float threshold);

protected:
    std::vector<PipelineMetaData> pipelineMetaData() final;

    void initBuffers();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    VkDeviceSize computeVertxBufferSize() const;

private:
    Voxels* _voxels{};
    VulkanBuffer _edgeLUT;
    VulkanBuffer _triangleLUT;
    float _minGridSize{};

    Mesh _mesh;

    struct {
        glm::vec3 bMin{0};
        float gridSize{1};

        glm::vec3 bMax{1};
        float isoLevel{0};
    } _constants;

    VulkanDescriptorSetLayout _lutDescriptorSetLayout;
    VkDescriptorSet _lutDescriptorSet{};

    VulkanDescriptorSetLayout _vertexDescriptorSetLayout;
    VkDescriptorSet _vertexDescriptorSet{};

    static constexpr uint32_t MaxTriangleVertices = 16u;
};