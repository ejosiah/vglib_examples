#pragma once

// Project includes
#include "cbt/large/cbt_utility.h"

// Project includes
#include "mesh.hpp"

#include <VulkanDevice.h>
#include <cstdint>
#include <string>

#include "ComputePipelins.hpp"

class Planet;

class MeshUpdater
{
public:
    // Cst & Dst
    MeshUpdater() = default;
    MeshUpdater(VulkanDevice& device, VulkanDescriptorSetLayout globalDescriptorSetLayout);

    // Allocate and release the resources
    void initialize();

    // Reload shaders
    bool reload_shaders(const std::string& shaderLibrary, CBTType cbtType, const char* updateShader = "UpdateMesh.compute");

    // Update a given mesh
    void update(VkCommandBuffer cmd, VkDescriptorSet globalDescriptorSetLayout, Planet& planet);

    // Make sure the mesh's topology is valid
    void validate(VkCommandBuffer cmd, const CBTMesh& mesh, VulkanBuffer geometryCB);

    // Reset the buffers
    void reset_buffers(VkCommandBuffer cmd,  VkDescriptorSet meshDescriptorSet, VkDescriptorSet cbtDescriptorSet) const;

    void classify(VkCommandBuffer cmd, VkDescriptorSet globalDescriptorSetLayout, const CBTMesh& mesh) const;

    void split(VkCommandBuffer cmd, const CBTMesh& mesh) const;

    void allocate(VkCommandBuffer cmd, const CBTMesh& mesh) const;

    void copy_neighbors(VkCommandBuffer cmd, const CBTMesh& mesh) const;

    void bisect(VkCommandBuffer cmd, VkDescriptorSet globalDescriptorSetLayout, const CBTMesh& mesh) const;

    void propagate_bisect(VkCommandBuffer cmd, const CBTMesh& mesh) const;

    // Prepare the mesh for indirect dispatches
    void prepare_indirection(VkCommandBuffer cmd, VkDescriptorSet meshDescriptorSet, int32_t bufferIndex, uint32_t gx, const std::string& section) const;

    void prepare_indirection(VkCommandBuffer cmd, const Planet& planet) const;

    void prepare_simplify(VkCommandBuffer cmd, VkDescriptorSet globalDescriptorSetLayout, const CBTMesh& mesh) const;

    void simplify(VkCommandBuffer cmd, VkDescriptorSet globalDescriptorSetLayout, const CBTMesh& mesh) const;

    void propagate_simplify(VkCommandBuffer cmd, const CBTMesh& mesh) const;

    void reduce(VkCommandBuffer cmd, const CBTMesh& mesh) const;

    // Control if the validation pass should run
    bool check_if_valid();

    // Query occupancy
    void query_occupancy(VkCommandBuffer cmdB, const CBTMesh& mesh);

    // Get the occupancy
    uint32_t get_occupancy();

protected:
    std::vector<PipelineMetaData> metadata();

    void createDescriptorSetLayout();

    void updateDescriptorSet();

    void createPipelines();

private:
    // Graphics Device
    VulkanDevice* m_Device{};
    VulkanDescriptorSetLayout m_globalDescriptorSetLayout;
    VkDescriptorSet m_globalDescriptorSet{};

    // Buffer that can be shared between the updates
    VulkanBuffer indirectBuffer;
    VulkanBuffer memoryBuffer;
    VulkanBuffer validationBuffer;
    VulkanBuffer validationBufferRB;
    VulkanBuffer occupancyBufferRB;

    VulkanDescriptorSetLayout m_descriptorSetLayout;
    VkDescriptorSet m_descriptorSet{};

    ComputePipelines m_compute;
    uint32_t m_currentNeighborsBufferIdx{};
    uint32_t m_nextNeighborsBufferIdx{};

    // Main update
    // ComputeShader m_ResetCS = 0;
    // ComputeShader m_ClassifyCS = 0;
    // ComputeShader m_SplitCS = 0;
    // ComputeShader m_PrepareIndirectCS = 0;
    // ComputeShader m_AllocateCS = 0;
    // ComputeShader m_BisectCS = 0;
    // ComputeShader m_PropagateBisectCS = 0;
    // ComputeShader m_PrepareSimplifyCS = 0;
    // ComputeShader m_SimplifyCS = 0;
    // ComputeShader m_PropagateSimplifyCS = 0;

    // Reduction
    // ComputeShader m_ReducePrePassCS = 0;
    // ComputeShader m_ReduceFirstPassCS = 0;
    // ComputeShader m_ReduceSecondPassCS = 0;

    // Indexation
    // ComputeShader m_BisectorIndexationCS = 0;
    // ComputeShader m_PrepareBisectorIndirectCS = 0;

    // Debug
    // ComputeShader m_ValidateCS = 0;
};
