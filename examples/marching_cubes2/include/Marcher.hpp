#pragma once

#include "VulkanDevice.h"
#include "ComputePipelins.hpp"
#include "Voxel.hpp"

class Marcher final : public ComputePipelines {
public:
    Marcher() = default;

    Marcher(Voxels* voxels, float minGridSize);

    void init();

    VulkanBuffer generateMesh(float gridSize);

    void generateMesh(VkCommandBuffer commandBuffer, float gridSize);

protected:
    std::vector<PipelineMetaData> pipelineMetaData() final;

    void initBuffers();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    VkDeviceSize computeVertxBufferSize() const;

private:
    Voxels* _voxels;
    VulkanBuffer _vertices;
    VulkanBuffer _normals;
    VulkanBuffer _edgeLUT;
    VulkanBuffer _triangleLUT;
    VulkanBuffer _vertexCount;
    float _minGridSize;

    struct {
        VulkanBuffer vertices;
    } _mesh;

    struct {
        glm::vec3 bMin{0};
        float gridSize{1};

        glm::vec3 bMax{1};
        float isoLevel{1e-6};
    } _constants;

    VulkanDescriptorSetLayout _lutDescriptorSetLayout;
    VkDescriptorSet _lutDescriptorSet{};

    VulkanDescriptorSetLayout _vertexDescriptorSetLayout;
    VkDescriptorSet _vertexDescriptorSet{};

    static constexpr uint32_t MaxTriangleVertices = 16u;
};