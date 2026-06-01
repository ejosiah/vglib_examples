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
    friend class EarthRenderer;
    friend class WaterDeformer;
    friend class MeshUpdater;
    friend class PlanetDemo;

    struct Params {
        std::string name;
        VulkanDevice& device;
        VulkanDescriptorSetLayout globalDescriptorSetLayout;
        float planetRadius;
        glm::dvec3 planetCenter;
        float toggleDistance;
        float triangleSize;
        uint32_t materialID;
    };

    static VulkanDescriptorSetLayout meshDescriptorSetLayout;
    static VulkanDescriptorSetLayout cbtDescriptorSetLayout;
    static VulkanDescriptorSetLayout lebDescriptorSetLayout;

    VkDescriptorSet m_descriptorSet{};
    VkDescriptorSet m_CBTDescriptorSet{};
    VkDescriptorSet m_LEBDescriptorSet{};

    Planet() = default;

    explicit Planet(const Params& params);

    void initialize(const cbt_large::CBT& cbt, const CPUMesh& mesh);

    void createDescriptorSetLayouts();

    void createMeshDescriptorSetLayout();

    static void createMeshDescriptorSetLayout(VulkanDevice& device);

    void createCBTDescriptorSetLayout();

    static void createCBTDescriptorSetLayout(VulkanDevice& device);

    void createLEBDescriptorSetLayout();

    static void createLEBDescriptorSetLayout(VulkanDevice& device);

    void updateDescriptorSet();

    void updateCBTDescriptorSet();

    void updateLEBDescriptorSet(const VulkanBuffer& lebMatrixCache);

    void clear(VkCommandBuffer cmd);

    void evaluate_leb(VkCommandBuffer cmd, VkDescriptorSet globalDescriptorSet, bool clearBuffer, bool complete = false);

    void update_constant_buffers(const UpdateCB& updateCb);

    const GeometryCB& get_geometry_cb_data() const { return m_GeometryCBData; }
    const PlanetCB& get_planet_cb_data() const { return m_PlanetCBData; }
    const UpdateCB& get_update_cb_data() const { return *m_UpdateCB.cpu; }

    void createDebugPipeline();

    void renderDebug(VkCommandBuffer commandBuffer, VkDescriptorSet globalDescriptorSet);

protected:
    std::vector<PipelineMetaData> metadata();

    void createPipelines();

private:
    // General
    std::string m_name;
    VulkanDevice* m_device{};
    ComputePipelines m_compute;
    VulkanDescriptorSetLayout m_globalDescriptorSetLayout;

    Pipeline m_debug;

    // Static properties
    float m_PlanetRadius = 0.0;
    float m_ToggleDistance = 0.0;
    glm::dvec3 m_PlanetCenter = { 0.0, 0.0, 0.0 };
    uint32_t m_MaterialID = UINT32_MAX;

    // Modifiable properties
    int32_t m_MaxSubdivisionDepth = 63;
    float m_TriangleSize = 60.0;

public:
    // Runtime resources
    CBTMesh m_CBTMesh;
    BaseMesh m_BaseMesh;
    VulkanBuffer m_GeometryCB; // ConstantBuffer
    VulkanBuffer m_PlanetCB; // ConstantBuffer
    GeometryCB m_GeometryCBData{};
    PlanetCB m_PlanetCBData{};

    struct {
        VulkanBuffer gpu;
        UpdateCB* cpu{};
    } m_UpdateCB;
};
