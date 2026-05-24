#include "planet.hpp"
#include "constant_buffers.hpp"
#include "AppContext.hpp"
#include <fmt/format.h>

namespace {
    bool descriptorSetLayoutInitialized = false;
    bool cbtDescriptorSetLayoutInitialized = false;

    VkDescriptorBufferInfo descriptor_buffer_info(const VulkanBuffer& buffer) { return { buffer, 0, VK_WHOLE_SIZE }; }

    void set_buffer_write(VkWriteDescriptorSet& write, uint32_t binding, VkDescriptorType descriptorType, const VkDescriptorBufferInfo* bufferInfo, uint32_t descriptorCount = 1) {
        write.dstBinding = binding;
        write.descriptorType = descriptorType;
        write.descriptorCount = descriptorCount;
        write.pBufferInfo = bufferInfo;
    }
}

VulkanDescriptorSetLayout Planet::descriptorSetLayout;
VulkanDescriptorSetLayout Planet::cbtDescriptorSetLayout;

Planet::Planet(VulkanDevice& device, const std::string& name, float planetRadius, const glm::dvec3 &planetCenter, float toggleDistance,
    float triangleSize, uint32_t materialID)
        : m_Device{&device}
        , m_name(name)
        , m_PlanetRadius(planetRadius)
        , m_PlanetCenter(planetCenter)
        , m_ToggleDistance(toggleDistance)
        , m_TriangleSize(triangleSize)
        , m_MaterialID(materialID) {}

void Planet::initialize(const cbt_large::CBT &cbt, const CPUMesh &mesh) {

    m_GeometryCB = m_Device->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(GeometryCB), fmt::format("{}_geometry_cb", m_name));
    m_PlanetCB = m_Device->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(PlanetCB), fmt::format("{}_planet_cb", m_name));
    m_UpdateCB = m_Device->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(UpdateCB), fmt::format("{}_update_cb", m_name));

    initialize_cbt_mesh(mesh, cbt, *m_Device, m_CBTMesh);
    initialize_base_mesh(mesh, *m_Device, m_BaseMesh);
    createDescriptorSetLayout();
    createCBTDescriptorSetLayout();
    updateDescriptorSet();
    updateCBTDescriptorSet();
}

void Planet::createDescriptorSetLayout() { createDescriptorSetLayout(*m_Device); }

void Planet::createDescriptorSetLayout(VulkanDevice& device) {
    if (descriptorSetLayoutInitialized) return;

    descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("planet_descriptor_set_layout")
            .binding(0)  // m_GeometryCB
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)  // m_updateCB
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(2) // m_CBTMesh.neighborsBuffers
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(2)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(3) // m_CBTMesh.currentVertexBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(4)  // m_CBTMesh.indirectDrawBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(5)  // m_CBTMesh.indirectDispatchBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(6)  // m_CBTMesh.indexedBisectorBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(7)  // m_CBTMesh.visibleIndexedBisectorBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(8)  // m_CBTMesh.modifiedIndexedBisectorBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(9)  // m_CBTMesh.heapIDBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(10)  // m_CBTMesh.updateBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(11)  // m_CBTMesh.classificationBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(12)  // m_CBTMesh.simplificationBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(13)  // m_CBTMesh.allocateBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(14)  // m_CBTMesh.propagateBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    descriptorSetLayoutInitialized = true;
}

void Planet::createCBTDescriptorSetLayout() { createCBTDescriptorSetLayout(*m_Device); }

void Planet::createCBTDescriptorSetLayout(VulkanDevice& device) {
    if (cbtDescriptorSetLayoutInitialized) return;

    cbtDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("planet_cbt_descriptor_set_layout")
            .binding(0)  // m_CBTMesh.gpuCBT.bufferArray[0]
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)  // m_CBTMesh.gpuCBT.bufferArray[1]
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    cbtDescriptorSetLayoutInitialized = true;
}

void Planet::updateDescriptorSet() {
    createDescriptorSetLayout();
    if (m_descriptorSet == VK_NULL_HANDLE) m_descriptorSet = AppContext::descriptorPool().allocate({ descriptorSetLayout }).front();
    m_CBTMesh.descriptorSet = m_descriptorSet;

    VkDescriptorBufferInfo geometryInfo = descriptor_buffer_info(m_GeometryCB);
    VkDescriptorBufferInfo updateInfo = descriptor_buffer_info(m_UpdateCB);
    VkDescriptorBufferInfo neighborsInfo[] = { descriptor_buffer_info(m_CBTMesh.neighborsBuffers[0]), descriptor_buffer_info(m_CBTMesh.neighborsBuffers[1]) };
    VkDescriptorBufferInfo currentVertexInfo = descriptor_buffer_info(m_CBTMesh.currentVertexBuffer);
    VkDescriptorBufferInfo indirectDrawInfo = descriptor_buffer_info(m_CBTMesh.indirectDrawBuffer);
    VkDescriptorBufferInfo indirectDispatchInfo = descriptor_buffer_info(m_CBTMesh.indirectDispatchBuffer);
    VkDescriptorBufferInfo indexedBisectorInfo = descriptor_buffer_info(m_CBTMesh.indexedBisectorBuffer);
    VkDescriptorBufferInfo visibleIndexedBisectorInfo = descriptor_buffer_info(m_CBTMesh.visibleIndexedBisectorBuffer);
    VkDescriptorBufferInfo modifiedIndexedBisectorInfo = descriptor_buffer_info(m_CBTMesh.modifiedIndexedBisectorBuffer);
    VkDescriptorBufferInfo heapIDInfo = descriptor_buffer_info(m_CBTMesh.heapIDBuffer);
    VkDescriptorBufferInfo bisectorDataInfo = descriptor_buffer_info(m_CBTMesh.updateBuffer);
    VkDescriptorBufferInfo classificationInfo = descriptor_buffer_info(m_CBTMesh.classificationBuffer);
    VkDescriptorBufferInfo simplificationInfo = descriptor_buffer_info(m_CBTMesh.simplificationBuffer);
    VkDescriptorBufferInfo allocateInfo = descriptor_buffer_info(m_CBTMesh.allocateBuffer);
    VkDescriptorBufferInfo propagateInfo = descriptor_buffer_info(m_CBTMesh.propagateBuffer);

    auto writes = initializers::writeDescriptorSets<15>(m_descriptorSet);
    set_buffer_write(writes[0], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &geometryInfo);
    set_buffer_write(writes[1], 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, &updateInfo);
    set_buffer_write(writes[2], 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, neighborsInfo, 2);
    set_buffer_write(writes[3], 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &currentVertexInfo);
    set_buffer_write(writes[4], 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &indirectDrawInfo);
    set_buffer_write(writes[5], 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &indirectDispatchInfo);
    set_buffer_write(writes[6], 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &indexedBisectorInfo);
    set_buffer_write(writes[7], 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &visibleIndexedBisectorInfo);
    set_buffer_write(writes[8], 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &modifiedIndexedBisectorInfo);
    set_buffer_write(writes[9], 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &heapIDInfo);
    set_buffer_write(writes[10], 10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bisectorDataInfo);
    set_buffer_write(writes[11], 11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &classificationInfo);
    set_buffer_write(writes[12], 12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &simplificationInfo);
    set_buffer_write(writes[13], 13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &allocateInfo);
    set_buffer_write(writes[14], 14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &propagateInfo);
    m_Device->updateDescriptorSets(writes);
}

void Planet::updateCBTDescriptorSet() {
    createCBTDescriptorSetLayout();
    if (m_CBTDescriptorSet == VK_NULL_HANDLE) m_CBTDescriptorSet = AppContext::descriptorPool().allocate({ cbtDescriptorSetLayout }).front();
    m_CBTMesh.cbtDescriptorSet = m_CBTDescriptorSet;

    VkDescriptorBufferInfo cbtTreeInfo = descriptor_buffer_info(m_CBTMesh.gpuCBT.bufferArray[0]);
    VkDescriptorBufferInfo cbtBitfieldInfo = descriptor_buffer_info(m_CBTMesh.gpuCBT.bufferArray[1]);

    auto writes = initializers::writeDescriptorSets<2>(m_CBTDescriptorSet);
    set_buffer_write(writes[0], 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &cbtTreeInfo);
    set_buffer_write(writes[1], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &cbtBitfieldInfo);
    m_Device->updateDescriptorSets(writes);
}
