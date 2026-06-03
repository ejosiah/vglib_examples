#include "planet.hpp"
#include "constant_buffers.hpp"
#include "AppContext.hpp"
#include "Barrier.hpp"
#include "descriptor_utils.hpp"
#include "filemanager.hpp"
#include <fmt/format.h>

namespace {
    constexpr auto Clear = "Clear";
    constexpr auto LebEval = "LebEval";
    constexpr uint32_t WorkgroupSize = 64;

    bool descriptorSetLayoutInitialized = false;
    bool cbtDescriptorSetLayoutInitialized = false;
    bool lebDescriptorSetLayoutInitialized = false;

}

VulkanDescriptorSetLayout Planet::meshDescriptorSetLayout;
VulkanDescriptorSetLayout Planet::cbtDescriptorSetLayout;
VulkanDescriptorSetLayout Planet::lebDescriptorSetLayout;

Planet::Planet(const Params& params)
        : m_name(params.name)
        , m_device{&params.device}
        , m_globalDescriptorSetLayout(params.globalDescriptorSetLayout)
        , m_PlanetRadius(params.planetRadius)
        , m_PlanetCenter(params.planetCenter)
        , m_ToggleDistance(params.toggleDistance)
        , m_TriangleSize(params.triangleSize)
        , m_MaterialID(params.materialID) {}

void Planet::initialize(const cbt_large::CBT& cbt, const CPUMesh& mesh) {

    initialize_cbt_mesh(mesh, cbt, *m_device, m_CBTMesh);
    initialize_base_mesh(mesh, *m_device, m_BaseMesh);

    m_PlanetCBData = PlanetCB{ PlanetVec3(m_PlanetCenter), PlanetScalar(m_PlanetRadius) };
    m_PlanetCB = m_device->createDeviceLocalBuffer(&m_PlanetCBData, sizeof(PlanetCB), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    m_device->setName<VK_OBJECT_TYPE_BUFFER>(fmt::format("{}_planet_cb", m_name), m_PlanetCB.buffer);


    m_GeometryCBData = GeometryCB {
        .TotalNumElements = m_CBTMesh.totalNumElements,
        .BaseDepth = m_CBTMesh.baseDepth,
        .TotalNumVertices = m_CBTMesh.totalNumElements * 3,
        .MaterialID = m_MaterialID
    };
    m_GeometryCB = m_device->createDeviceLocalBuffer(&m_GeometryCBData, sizeof(GeometryCB), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    m_device->setName<VK_OBJECT_TYPE_BUFFER>(fmt::format("{}_geometry_cb", m_name), m_GeometryCB.buffer);


    m_UpdateCB.gpu = m_device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(UpdateCB), fmt::format("{}_update_cb", m_name));
    m_UpdateCB.cpu = static_cast<UpdateCB*>(m_UpdateCB.gpu.map());
    m_UpdateCB.cpu->ViewProjectionMatrix[3] = PlanetVec4{0, 0, 0, 1};
    m_UpdateCB.cpu->InvViewProjectionMatrix[3] = PlanetVec4{0, 0, 0, 1};

    createDescriptorSetLayouts();
    createPipelines();
}

void Planet::createDescriptorSetLayouts() {
    createMeshDescriptorSetLayout();
    createCBTDescriptorSetLayout();
    createLEBDescriptorSetLayout();
    updateDescriptorSet();
    updateCBTDescriptorSet();
    createDebugPipeline();
}

void Planet::createDebugPipeline() {
    m_debug.pipeline =
    AppContext::prototypes().cloneGraphicsPipeline()
        .shaderStage()
            .vertexShader(FileManager::resource("planet_debug.vert.spv"))
            .fragmentShader(FileManager::resource("flat.frag.spv"))
        .vertexInputState().clear()
        .rasterizationState()
        .layout().clear()
            .addDescriptorSetLayout(m_globalDescriptorSetLayout)
            .addDescriptorSetLayout(meshDescriptorSetLayout)
            .addDescriptorSetLayout(cbtDescriptorSetLayout)
            .addDescriptorSetLayout(lebDescriptorSetLayout)
        .name(fmt::format("{}_debug", m_name))
    .build(m_debug.layout);
}

void Planet::renderDebug(VkCommandBuffer commandBuffer, VkDescriptorSet globalDescriptorSet) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debug.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debug.layout.handle, 0, 1, &globalDescriptorSet, 0, nullptr);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debug.layout.handle, 1, 1, &m_descriptorSet, 0, nullptr);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debug.layout.handle, 2, 1, &m_CBTDescriptorSet, 0, nullptr);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debug.layout.handle, 3, 1, &m_LEBDescriptorSet, 0, nullptr);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

std::vector<PipelineMetaData> Planet::metadata() {
    return {
        {
            .name = Clear,
            .shadePath = FileManager::resource("planet_clear.comp.spv"),
            .layouts = { &meshDescriptorSetLayout },
        },
        {
            .name = LebEval,
            .shadePath = FileManager::resource("planet_evaluate_leb.comp.spv"),
            .layouts = { &m_globalDescriptorSetLayout, &meshDescriptorSetLayout, &lebDescriptorSetLayout },
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) } },
        },
    };
}

void Planet::createPipelines() {
    m_compute = ComputePipelines{ m_device, metadata() };
    m_compute.createPipelines();
}

void Planet::createMeshDescriptorSetLayout() {
    createMeshDescriptorSetLayout(*m_device);
}

void Planet::createMeshDescriptorSetLayout(VulkanDevice& device) {
    if (descriptorSetLayoutInitialized) return;

    meshDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("planet_descriptor_set_layout")
            .binding(0)  // m_GeometryCB
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)  // m_updateCB
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(2) // m_CBTMesh.neighborsBuffers
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(2)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(3) // m_CBTMesh.currentVertexBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(4)  // m_CBTMesh.indirectDrawBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(5)  // m_CBTMesh.indirectDispatchBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(6)  // m_CBTMesh.indexedBisectorBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(7)  // m_CBTMesh.visibleIndexedBisectorBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(8)  // m_CBTMesh.modifiedIndexedBisectorBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(9)  // m_CBTMesh.heapIDBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(10)  // m_CBTMesh.updateBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(11)  // work list buffers
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(4)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(12)  // m_BaseMesh.vertexBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(13)  // m_CBTMesh.lebVertexBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(14)  // m_PlanetCB
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(15)  // m_CBTMesh.currentDisplacementBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();

    descriptorSetLayoutInitialized = true;
}

void Planet::createCBTDescriptorSetLayout() { createCBTDescriptorSetLayout(*m_device); }

void Planet::createCBTDescriptorSetLayout(VulkanDevice& device) {
    if (cbtDescriptorSetLayoutInitialized) return;

    cbtDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("planet_cbt_descriptor_set_layout")
            .binding(0)  // m_CBTMesh.gpuCBT.bufferArray[0]
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)  // m_CBTMesh.gpuCBT.bufferArray[1]
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();

    cbtDescriptorSetLayoutInitialized = true;
}

void Planet::createLEBDescriptorSetLayout() { createLEBDescriptorSetLayout(*m_device); }

void Planet::createLEBDescriptorSetLayout(VulkanDevice& device) {
    if (lebDescriptorSetLayoutInitialized) return;

    lebDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("planet_leb_descriptor_set_layout")
            .binding(0)  // LEB matrix cache
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();

    lebDescriptorSetLayoutInitialized = true;
}

void Planet::updateDescriptorSet() {
    auto sets = AppContext::descriptorPool().allocate({ meshDescriptorSetLayout });
    m_descriptorSet = sets[0];
    m_CBTMesh.descriptorSet = sets[0];

    m_device->setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>(fmt::format("{}_mesh_descriptor_set", m_name), m_descriptorSet);

    VkDescriptorBufferInfo geometryInfo = descriptor_buffer_info(m_GeometryCB);
    VkDescriptorBufferInfo updateInfo = descriptor_buffer_info(m_UpdateCB.gpu);
    VkDescriptorBufferInfo neighborsInfo[] = { descriptor_buffer_info(m_CBTMesh.neighborsBuffers[0]), descriptor_buffer_info(m_CBTMesh.neighborsBuffers[1]) };
    VkDescriptorBufferInfo currentVertexInfo = descriptor_buffer_info(m_CBTMesh.currentVertexBuffer);
    VkDescriptorBufferInfo currentDisplacementInfo = descriptor_buffer_info(m_CBTMesh.currentDisplacementBuffer);
    VkDescriptorBufferInfo indirectDrawInfo = descriptor_buffer_info(m_CBTMesh.indirectDrawBuffer);
    VkDescriptorBufferInfo indirectDispatchInfo = descriptor_buffer_info(m_CBTMesh.indirectDispatchBuffer);
    VkDescriptorBufferInfo indexedBisectorInfo = descriptor_buffer_info(m_CBTMesh.indexedBisectorBuffer);
    VkDescriptorBufferInfo visibleIndexedBisectorInfo = descriptor_buffer_info(m_CBTMesh.visibleIndexedBisectorBuffer);
    VkDescriptorBufferInfo modifiedIndexedBisectorInfo = descriptor_buffer_info(m_CBTMesh.modifiedIndexedBisectorBuffer);
    VkDescriptorBufferInfo heapIDInfo = descriptor_buffer_info(m_CBTMesh.heapIDBuffer);
    VkDescriptorBufferInfo bisectorDataInfo = descriptor_buffer_info(m_CBTMesh.updateBuffer);
    VkDescriptorBufferInfo baseVertexInfo = descriptor_buffer_info(m_BaseMesh.vertexBuffer);
    VkDescriptorBufferInfo lebVertexInfo = descriptor_buffer_info(m_CBTMesh.lebVertexBuffer);
    VkDescriptorBufferInfo planetInfo = descriptor_buffer_info(m_PlanetCB);
    VkDescriptorBufferInfo workListInfos[] = {
        descriptor_buffer_info(m_CBTMesh.classificationBuffer),
        descriptor_buffer_info(m_CBTMesh.simplificationBuffer),
        descriptor_buffer_info(m_CBTMesh.allocateBuffer),
        descriptor_buffer_info(m_CBTMesh.propagateBuffer),
    };

    auto writes = initializers::writeDescriptorSets<16>(m_descriptorSet);
    set_buffer_write(writes[0], 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &geometryInfo);
    set_buffer_write(writes[1], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &updateInfo);
    set_buffer_write(writes[2], 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, neighborsInfo, 2);
    set_buffer_write(writes[3], 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &currentVertexInfo);
    set_buffer_write(writes[4], 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &indirectDrawInfo);
    set_buffer_write(writes[5], 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &indirectDispatchInfo);
    set_buffer_write(writes[6], 6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &indexedBisectorInfo);
    set_buffer_write(writes[7], 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &visibleIndexedBisectorInfo);
    set_buffer_write(writes[8], 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &modifiedIndexedBisectorInfo);
    set_buffer_write(writes[9], 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &heapIDInfo);
    set_buffer_write(writes[10], 10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &bisectorDataInfo);
    set_buffer_write(writes[11], 11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, workListInfos, 4);
    set_buffer_write(writes[12], 12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &baseVertexInfo);
    set_buffer_write(writes[13], 13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lebVertexInfo);
    set_buffer_write(writes[14], 14, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &planetInfo);
    set_buffer_write(writes[15], 15, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &currentDisplacementInfo);
    m_device->updateDescriptorSets(writes);
}

void Planet::updateCBTDescriptorSet() {
    m_CBTDescriptorSet = AppContext::descriptorPool().allocate({ cbtDescriptorSetLayout }).front();
    m_CBTMesh.cbtDescriptorSet = m_CBTDescriptorSet;

    m_device->setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>(fmt::format("{}_cbt_descriptor_set", m_name), m_CBTDescriptorSet);

    VkDescriptorBufferInfo cbtTreeInfo = descriptor_buffer_info(m_CBTMesh.gpuCBT.bufferArray[0]);
    VkDescriptorBufferInfo cbtBitfieldInfo = descriptor_buffer_info(m_CBTMesh.gpuCBT.bufferArray[1]);

    auto writes = initializers::writeDescriptorSets<2>(m_CBTDescriptorSet);
    set_buffer_write(writes[0], 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &cbtTreeInfo);
    set_buffer_write(writes[1], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &cbtBitfieldInfo);
    m_device->updateDescriptorSets(writes);
}

void Planet::updateLEBDescriptorSet(const VulkanBuffer& lebMatrixCache) {
    m_LEBDescriptorSet = AppContext::descriptorPool().allocate({ lebDescriptorSetLayout }).front();
    m_device->setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>(fmt::format("{}_leb_descriptor_set", m_name), m_LEBDescriptorSet);

    VkDescriptorBufferInfo lebMatrixCacheInfo = descriptor_buffer_info(lebMatrixCache);

    auto writes = initializers::writeDescriptorSets<1>(m_LEBDescriptorSet);
    set_buffer_write(writes[0], 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &lebMatrixCacheInfo);
    m_device->updateDescriptorSets(writes);
}

void Planet::clear(VkCommandBuffer cmd) {
    m_device->section([&] {
        const auto layout = m_compute.layout(Clear);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(Clear));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &m_descriptorSet, 0, nullptr);
        vkCmdDispatch(cmd, (m_CBTMesh.totalNumElements * 3 + WorkgroupSize - 1) / WorkgroupSize, 1, 1);
        Barrier::computeWriteToRead(cmd);
    }, cmd, "clear");
}

void Planet::evaluate_leb(VkCommandBuffer cmd, VkDescriptorSet globalDescriptorSet, bool clearBuffer, bool complete) {
    if (clearBuffer) clear(cmd);

    m_device->section([&] {
        const auto layout = m_compute.layout(LebEval);
        const uint32_t completeValue = complete ? 1u : 0u;
        const VkDeviceSize indirectOffset = complete ? 0 : sizeof(VkDispatchIndirectCommand) * 2;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(LebEval));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &globalDescriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1, 1, &m_descriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 2, 1, &m_LEBDescriptorSet, 0, nullptr);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(completeValue), &completeValue);
        vkCmdDispatchIndirect(cmd, m_CBTMesh.indirectDispatchBuffer, indirectOffset);
        Barrier::computeWriteToRead(cmd);
    }, cmd, "evaluate_leb");
}

void Planet::update_constant_buffers(const UpdateCB& updateCb) {
    *m_UpdateCB.cpu = updateCb;
    m_UpdateCB.cpu->MaxSubdivisionDepth = m_MaxSubdivisionDepth;
    m_UpdateCB.cpu->TriangleSize = m_TriangleSize;
}
