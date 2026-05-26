#pragma once

#include "mesh.hpp"
#include <VulkanDevice.h>
#include <ComputePipelins.hpp>
#include <glm/glm.hpp>

#include <string>
#include <vector>

#include "constant_buffers.hpp"

class Planet {
public:
    Planet() = default;

    Planet(VulkanDevice& device, const std::string& name, float planetRadius, const glm::dvec3& planetCenter, float toggleDistance,
         float triangleSize, uint32_t materialID);

    void initialize(const cbt_large::CBT& cbt, const CPUMesh& mesh, VulkanDescriptorSetLayout globalDescriptorSetLayout);

    void createDescriptorSetLayout();

    static void createDescriptorSetLayout(VulkanDevice& device);

    void createCBTDescriptorSetLayout();

    static void createCBTDescriptorSetLayout(VulkanDevice& device);

    void createLEBDescriptorSetLayout();

    static void createLEBDescriptorSetLayout(VulkanDevice& device);

    void updateDescriptorSet();

    void updateCBTDescriptorSet();

    void updateLEBDescriptorSet(const VulkanBuffer& lebMatrixCache);

    void clear(VkCommandBuffer cmd);

    void evaluate_leb(VkCommandBuffer cmd, VkDescriptorSet globalDescriptorSet, bool clearBuffer, bool complete = false);

protected:
    std::vector<PipelineMetaData> metadata();

    void createPipelines();

public:
    // General
    VulkanDevice* m_Device{};
    VulkanDescriptorSetLayout m_globalDescriptorSetLayout;
    std::string m_name;

    // Static properties
    float m_PlanetRadius = 0.0;
    float m_ToggleDistance = 0.0;
    glm::dvec3 m_PlanetCenter = { 0.0, 0.0, 0.0 };
    uint32_t m_MaterialID = UINT32_MAX;

    // Modifiable properties
    int32_t m_MaxSubdivisionDepth = 63;
    float m_TriangleSize = 0.0;

    // Runtime resources
    CBTMesh m_CBTMesh;
    BaseMesh m_BaseMesh;
    VulkanBuffer m_GeometryCB; // ConstantBuffer
    VulkanBuffer m_PlanetCB; // ConstantBuffer

    struct {
        VulkanBuffer gpu;
        UpdateCB* cpu{};
    } m_UpdateCB;

    VkDescriptorSet m_descriptorSet{};
    VkDescriptorSet m_CBTDescriptorSet{};
    VkDescriptorSet m_LEBDescriptorSet{};
    // Compute shader
    // ComputeShader m_LebEvalCS = 0;
    // ComputeShader m_ClearCS = 0;
    ComputePipelines m_compute;
    static VulkanDescriptorSetLayout descriptorSetLayout;
    static VulkanDescriptorSetLayout cbtDescriptorSetLayout;
    static VulkanDescriptorSetLayout lebDescriptorSetLayout;

};
