#include "mesh_updater.hpp"
#include "AppContext.hpp"
#include "planet.hpp"
#include "filemanager.hpp"
#include <cinttypes>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <utility>

#include "Barrier.hpp"

namespace WorklistBuffer {
    constexpr int Classification = 0;
    constexpr int Simplification = 1;
    constexpr int Allocate = 2;
    constexpr int Propagate = 3;
}

namespace {
    constexpr auto Reset = "Reset";
    constexpr auto Classify = "Classify";
    constexpr auto PrepareIndirect = "PrepareIndirect";
    constexpr auto Split = "Split";

    constexpr auto Allocate128K = "Allocate128K";
    constexpr auto Allocate256K = "Allocate256K";
    constexpr auto Allocate512K = "Allocate512K";
    constexpr auto Allocate1M = "Allocate1M";

    constexpr auto Bisect128K = "Bisect128K";
    constexpr auto Bisect256K = "Bisect256K";
    constexpr auto Bisect512K = "Bisect512K";
    constexpr auto Bisect1M = "Bisect1M";

    constexpr auto PropagateBisect = "propagate_bisect";
    constexpr auto PrepareSimplify = "prepare_simplify";
    constexpr auto Simplify128K = "Simplify128K";
    constexpr auto Simplify256K = "Simplify256K";
    constexpr auto Simplify512K = "Simplify512K";
    constexpr auto Simplify1M = "Simplify1M";
    constexpr auto PropagateSimplify = "propagate_simplify";

    constexpr auto ReducePrePass128K = "ReducePrePass128K";
    constexpr auto ReducePrePass256K = "ReducePrePass256K";
    constexpr auto ReducePrePass512K = "ReducePrePass512K";
    constexpr auto ReducePrePass1M = "ReducePrePass1M";
    constexpr auto ReduceFirstPass128K = "ReduceFirstPass128K";
    constexpr auto ReduceFirstPass256K = "ReduceFirstPass256K";
    constexpr auto ReduceFirstPass512K = "ReduceFirstPass512K";
    constexpr auto ReduceFirstPass1M = "ReduceFirstPass1M";
    constexpr auto ReduceSecondPass128K = "ReduceSecondPass128K";
    constexpr auto ReduceSecondPass256K = "ReduceSecondPass256K";
    constexpr auto ReduceSecondPass512K = "ReduceSecondPass512K";
    constexpr auto ReduceSecondPass1M = "ReduceSecondPass1M";
    constexpr auto BisectorIndexation = "BisectorIndexation";
    constexpr auto PrepareBisectorIndirect = "PrepareBisectorIndirect";

    constexpr uint32_t WorkgroupSize = 64;
    constexpr auto StorageAndIndirectUsage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    constexpr auto DumpableStorageUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    int32_t CbtType128K = static_cast<int32_t>(CBTType::OCBT_128K);
    int32_t CbtType256K = static_cast<int32_t>(CBTType::OCBT_256K);
    int32_t CbtType512K = static_cast<int32_t>(CBTType::OCBT_512K);
    int32_t CbtType1M = static_cast<int32_t>(CBTType::OCBT_1M);

    struct NeighborBufferIndices {
        uint32_t current;
        uint32_t next;
    };

    VkDescriptorBufferInfo descriptor_buffer_info(const VulkanBuffer& buffer) { return { buffer, 0, VK_WHOLE_SIZE }; }

    void set_buffer_write(VkWriteDescriptorSet& write, uint32_t binding, const VkDescriptorBufferInfo* bufferInfo) {
        write.dstBinding = binding;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = bufferInfo;
    }

    const char* allocate_pipeline_name(CBTType cbtType) {
        switch (cbtType) {
            case CBTType::OCBT_128K: return Allocate128K;
            case CBTType::OCBT_256K: return Allocate256K;
            case CBTType::OCBT_512K: return Allocate512K;
            case CBTType::OCBT_1M: return Allocate1M;
            default: return Allocate128K;
        }
    }

    const char* bisect_pipeline_name(CBTType cbtType) {
        switch (cbtType) {
            case CBTType::OCBT_128K: return Bisect128K;
            case CBTType::OCBT_256K: return Bisect256K;
            case CBTType::OCBT_512K: return Bisect512K;
            case CBTType::OCBT_1M: return Bisect1M;
            default: return Bisect128K;
        }
    }

    const char* simplify_pipeline_name(CBTType cbtType) {
        switch (cbtType) {
            case CBTType::OCBT_128K: return Simplify128K;
            case CBTType::OCBT_256K: return Simplify256K;
            case CBTType::OCBT_512K: return Simplify512K;
            case CBTType::OCBT_1M: return Simplify1M;
            default: return Simplify128K;
        }
    }

    const char* reduce_prepass_pipeline_name(CBTType cbtType) {
        switch (cbtType) {
            case CBTType::OCBT_128K: return ReducePrePass128K;
            case CBTType::OCBT_256K: return ReducePrePass256K;
            case CBTType::OCBT_512K: return ReducePrePass512K;
            case CBTType::OCBT_1M: return ReducePrePass1M;
            default: return ReducePrePass128K;
        }
    }

    const char* reduce_first_pass_pipeline_name(CBTType cbtType) {
        switch (cbtType) {
            case CBTType::OCBT_128K: return ReduceFirstPass128K;
            case CBTType::OCBT_256K: return ReduceFirstPass256K;
            case CBTType::OCBT_512K: return ReduceFirstPass512K;
            case CBTType::OCBT_1M: return ReduceFirstPass1M;
            default: return ReduceFirstPass128K;
        }
    }

    const char* reduce_second_pass_pipeline_name(CBTType cbtType) {
        switch (cbtType) {
            case CBTType::OCBT_128K: return ReduceSecondPass128K;
            case CBTType::OCBT_256K: return ReduceSecondPass256K;
            case CBTType::OCBT_512K: return ReduceSecondPass512K;
            case CBTType::OCBT_1M: return ReduceSecondPass1M;
            default: return ReduceSecondPass128K;
        }
    }

    SpecializationConstants cbt_type_specialization(int32_t& cbtType) {
        return {
            .entries = { {0, 0, sizeof(cbtType)} },
            .data = &cbtType,
            .dataSize = sizeof(cbtType),
        };
    }
}

MeshUpdater::MeshUpdater(VulkanDevice &device, VulkanDescriptorSetLayout globalDescriptorSetLayout)
: m_Device(&device)
, m_globalDescriptorSetLayout(std::move(globalDescriptorSetLayout)){}

void MeshUpdater::initialize(VkDescriptorSet globalDescriptorSet) {
    m_globalDescriptorSet = globalDescriptorSet;
    indirectBuffer = m_Device->createBuffer(StorageAndIndirectUsage, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(VkDispatchIndirectCommand) * 3);
    memoryBuffer = m_Device->createBuffer(DumpableStorageUsage, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int32_t) * 2);
    validationBuffer = m_Device->createBuffer(DumpableStorageUsage, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int32_t) * 2);
    validationBufferRB = m_Device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(int32_t) * 2);
    occupancyBufferRB = m_Device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(uint32_t) * 2);

    createDescriptorSetLayout();
    updateDescriptorSet();
    createPipelines();
}

void MeshUpdater::update(VkCommandBuffer cmd, VkDescriptorSet globalDescriptorSet, Planet& planet) {
    auto& mesh = planet.m_CBTMesh;
    m_globalDescriptorSet = globalDescriptorSet;
    auto meshUpdaterSection = m_Device->section(cmd, "mesh_updater");
    m_currentNeighborsBufferIdx = mesh.currentNeighborsBufferIdx;
    m_nextNeighborsBufferIdx = (m_currentNeighborsBufferIdx + 1) % 2;

    Barrier::fragmentReadToComputeWrite(cmd);

    reset_buffers(cmd, mesh.descriptorSet, mesh.cbtDescriptorSet);

    classify(cmd, globalDescriptorSet, mesh);

    prepare_indirection(cmd, mesh.descriptorSet, WorklistBuffer::Classification, 2, "split");
    split(cmd, mesh);

    prepare_indirection(cmd, mesh.descriptorSet, WorklistBuffer::Allocate, 1, "allocate");
    allocate(cmd, mesh);

    copy_neighbors(cmd, mesh);
    bisect(cmd, globalDescriptorSet, mesh);
    prepare_indirection(cmd, mesh.descriptorSet, WorklistBuffer::Propagate, 1, "propagate_bisect");
    propagate_bisect(cmd, mesh);

    prepare_simplify(cmd, globalDescriptorSet, mesh);
    prepare_indirection(cmd, mesh.descriptorSet, WorklistBuffer::Simplification, 1, "simplify");
    simplify(cmd, globalDescriptorSet, mesh);
    prepare_indirection(cmd, mesh.descriptorSet, WorklistBuffer::Propagate, 2, "propagate_simplify");
    propagate_simplify(cmd, mesh);
    reduce(cmd, mesh);

    mesh.currentNeighborsBufferIdx = m_nextNeighborsBufferIdx;
    prepare_indirection(cmd, planet);
}

void MeshUpdater::createDescriptorSetLayout() {
    m_descriptorSetLayout =
        m_Device->descriptorSetLayoutBuilder()
            .name("mesh_updater_descriptor_set_layout")
            .binding(0)  // memoryBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)  // validationBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(2)  // indirectBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();
}

void MeshUpdater::updateDescriptorSet() {
    m_descriptorSet = AppContext::descriptorPool().allocate({ m_descriptorSetLayout }).front();

    m_Device->setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("mesh_updater_descriptor_set", m_descriptorSet);

    VkDescriptorBufferInfo memoryInfo = descriptor_buffer_info(memoryBuffer);
    VkDescriptorBufferInfo validationInfo = descriptor_buffer_info(validationBuffer);
    VkDescriptorBufferInfo indirectInfo = descriptor_buffer_info(indirectBuffer);

    auto writes = initializers::writeDescriptorSets<3>(m_descriptorSet);
    set_buffer_write(writes[0], 0, &memoryInfo);
    set_buffer_write(writes[1], 1, &validationInfo);
    set_buffer_write(writes[2], 2, &indirectInfo);
    m_Device->updateDescriptorSets(writes);
}

std::vector<PipelineMetaData> MeshUpdater::metadata() {
    return {
        {
            .name = Reset,
            .shadePath = FileManager::resource("mesh_reset.comp.spv"),
            .layouts = { &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout, &Planet::cbtDescriptorSetLayout },
        },
        {
            .name = Classify,
            .shadePath = FileManager::resource("mesh_classify.comp.spv"),
            .layouts = { &m_globalDescriptorSetLayout, &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout },
        },
        {
            .name = PrepareIndirect,
            .shadePath = FileManager::resource("mesh_prepare_indirect.comp.spv"),
            .layouts = { &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout },
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int32_t) } },
        },
        {
            .name = Split,
            .shadePath = FileManager::resource("mesh_split.comp.spv"),
            .layouts = { &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout },
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) } },
        },
        {
            .name = Allocate128K,
            .shadePath = FileManager::resource("mesh_allocate.comp.spv"),
            .layouts = { &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout, &Planet::cbtDescriptorSetLayout },
            .specializationConstants = cbt_type_specialization(CbtType128K),
        },
        {
            .name = Allocate256K,
            .shadePath = FileManager::resource("mesh_allocate.comp.spv"),
            .layouts = { &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout, &Planet::cbtDescriptorSetLayout },
            .specializationConstants = cbt_type_specialization(CbtType256K),
        },
        {
            .name = Allocate512K,
            .shadePath = FileManager::resource("mesh_allocate.comp.spv"),
            .layouts = { &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout, &Planet::cbtDescriptorSetLayout },
            .specializationConstants = cbt_type_specialization(CbtType512K),
        },
        {
            .name = Allocate1M,
            .shadePath = FileManager::resource("mesh_allocate.comp.spv"),
            .layouts = { &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout, &Planet::cbtDescriptorSetLayout },
            .specializationConstants = cbt_type_specialization(CbtType1M),
        },
        {
            .name = Bisect128K,
            .shadePath = FileManager::resource("mesh_bisect.comp.spv"),
            .layouts = { &m_globalDescriptorSetLayout, &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout, &Planet::cbtDescriptorSetLayout },
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NeighborBufferIndices) } },
            .specializationConstants = cbt_type_specialization(CbtType128K),
        },
        {
            .name = Bisect256K,
            .shadePath = FileManager::resource("mesh_bisect.comp.spv"),
            .layouts = { &m_globalDescriptorSetLayout, &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout, &Planet::cbtDescriptorSetLayout },
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NeighborBufferIndices) } },
            .specializationConstants = cbt_type_specialization(CbtType256K),
        },
        {
            .name = Bisect512K,
            .shadePath = FileManager::resource("mesh_bisect.comp.spv"),
            .layouts = { &m_globalDescriptorSetLayout, &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout, &Planet::cbtDescriptorSetLayout },
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NeighborBufferIndices) } },
            .specializationConstants = cbt_type_specialization(CbtType512K),
        },
        {
            .name = Bisect1M,
            .shadePath = FileManager::resource("mesh_bisect.comp.spv"),
            .layouts = { &m_globalDescriptorSetLayout, &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout, &Planet::cbtDescriptorSetLayout },
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NeighborBufferIndices) } },
            .specializationConstants = cbt_type_specialization(CbtType1M),
        },
        {
            .name = PropagateBisect,
            .shadePath = FileManager::resource("mesh_propagate_bisect.comp.spv"),
            .layouts = { &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout},
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NeighborBufferIndices) } },
        },
        {
            .name = PrepareSimplify,
            .shadePath = FileManager::resource("mesh_prepare_simplify.comp.spv"),
            .layouts = { &m_globalDescriptorSetLayout, &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout},
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NeighborBufferIndices) } },
        },
        {
            .name = Simplify128K,
            .shadePath = FileManager::resource("mesh_simplify.comp.spv"),
            .layouts = { &m_globalDescriptorSetLayout, &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout, &Planet::cbtDescriptorSetLayout },
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NeighborBufferIndices) } },
            .specializationConstants = cbt_type_specialization(CbtType128K),
        },
        {
            .name = Simplify256K,
            .shadePath = FileManager::resource("mesh_simplify.comp.spv"),
            .layouts = { &m_globalDescriptorSetLayout, &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout, &Planet::cbtDescriptorSetLayout },
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NeighborBufferIndices) } },
            .specializationConstants = cbt_type_specialization(CbtType256K),
        },
        {
            .name = Simplify512K,
            .shadePath = FileManager::resource("mesh_simplify.comp.spv"),
            .layouts = { &m_globalDescriptorSetLayout, &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout, &Planet::cbtDescriptorSetLayout },
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NeighborBufferIndices) } },
            .specializationConstants = cbt_type_specialization(CbtType512K),
        },
        {
            .name = Simplify1M,
            .shadePath = FileManager::resource("mesh_simplify.comp.spv"),
            .layouts = { &m_globalDescriptorSetLayout, &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout, &Planet::cbtDescriptorSetLayout },
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NeighborBufferIndices) } },
            .specializationConstants = cbt_type_specialization(CbtType1M),
        },
        {
            .name = PropagateSimplify,
            .shadePath = FileManager::resource("mesh_propagate_simplify.comp.spv"),
            .layouts = { &m_descriptorSetLayout, &Planet::meshDescriptorSetLayout },
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NeighborBufferIndices) } },
        },
        {
            .name = ReducePrePass128K,
            .shadePath = FileManager::resource("mesh_reduce_prepass.comp.spv"),
            .layouts = { &Planet::cbtDescriptorSetLayout },
            .specializationConstants = cbt_type_specialization(CbtType128K),
        },
        {
            .name = ReducePrePass256K,
            .shadePath = FileManager::resource("mesh_reduce_prepass.comp.spv"),
            .layouts = { &Planet::cbtDescriptorSetLayout },
            .specializationConstants = cbt_type_specialization(CbtType256K),
        },
        {
            .name = ReducePrePass512K,
            .shadePath = FileManager::resource("mesh_reduce_prepass.comp.spv"),
            .layouts = { &Planet::cbtDescriptorSetLayout },
            .specializationConstants = cbt_type_specialization(CbtType512K),
        },
        {
            .name = ReducePrePass1M,
            .shadePath = FileManager::resource("mesh_reduce_prepass.comp.spv"),
            .layouts = { &Planet::cbtDescriptorSetLayout },
            .specializationConstants = cbt_type_specialization(CbtType1M),
        },
        {
            .name = ReduceFirstPass128K,
            .shadePath = FileManager::resource("mesh_reduce_first_pass.comp.spv"),
            .layouts = { &Planet::cbtDescriptorSetLayout },
            .specializationConstants = cbt_type_specialization(CbtType128K),
        },
        {
            .name = ReduceFirstPass256K,
            .shadePath = FileManager::resource("mesh_reduce_first_pass.comp.spv"),
            .layouts = { &Planet::cbtDescriptorSetLayout },
            .specializationConstants = cbt_type_specialization(CbtType256K),
        },
        {
            .name = ReduceFirstPass512K,
            .shadePath = FileManager::resource("mesh_reduce_first_pass.comp.spv"),
            .layouts = { &Planet::cbtDescriptorSetLayout },
            .specializationConstants = cbt_type_specialization(CbtType512K),
        },
        {
            .name = ReduceFirstPass1M,
            .shadePath = FileManager::resource("mesh_reduce_first_pass.comp.spv"),
            .layouts = { &Planet::cbtDescriptorSetLayout },
            .specializationConstants = cbt_type_specialization(CbtType1M),
        },
        {
            .name = ReduceSecondPass128K,
            .shadePath = FileManager::resource("mesh_reduce_second_pass.comp.spv"),
            .layouts = { &Planet::cbtDescriptorSetLayout },
            .specializationConstants = cbt_type_specialization(CbtType128K),
        },
        {
            .name = ReduceSecondPass256K,
            .shadePath = FileManager::resource("mesh_reduce_second_pass.comp.spv"),
            .layouts = { &Planet::cbtDescriptorSetLayout },
            .specializationConstants = cbt_type_specialization(CbtType256K),
        },
        {
            .name = ReduceSecondPass512K,
            .shadePath = FileManager::resource("mesh_reduce_second_pass.comp.spv"),
            .layouts = { &Planet::cbtDescriptorSetLayout },
            .specializationConstants = cbt_type_specialization(CbtType512K),
        },
        {
            .name = ReduceSecondPass1M,
            .shadePath = FileManager::resource("mesh_reduce_second_pass.comp.spv"),
            .layouts = { &Planet::cbtDescriptorSetLayout },
            .specializationConstants = cbt_type_specialization(CbtType1M),
        },
        {
            .name = BisectorIndexation,
            .shadePath = FileManager::resource("mesh_bisector_indexation.comp.spv"),
            .layouts = { &Planet::meshDescriptorSetLayout, &m_globalDescriptorSetLayout },
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) } },
        },
        {
            .name = PrepareBisectorIndirect,
            .shadePath = FileManager::resource("mesh_prepare_bisector_indirect.comp.spv"),
            .layouts = { &Planet::meshDescriptorSetLayout },
        },
    };
}

void MeshUpdater::createPipelines() {
    m_compute = ComputePipelines{ m_Device, metadata() };
    m_compute.createPipelines();
}

void MeshUpdater::reset_buffers(VkCommandBuffer cmd, VkDescriptorSet meshDescriptorSet, VkDescriptorSet cbtDescriptorSet) const {
    m_Device->section([&] {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(Reset));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout(Reset), 0, 1, &m_descriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout(Reset), 1, 1, &meshDescriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout(Reset), 2, 1, &cbtDescriptorSet, 0, nullptr);
        vkCmdDispatch(cmd, 1, 1, 1);
        Barrier::computeWriteToRead(cmd);
    }, cmd, "reset");

}

void MeshUpdater::classify(VkCommandBuffer cmd, VkDescriptorSet globalDescriptorSetLayout, const CBTMesh& mesh) {
    m_Device->section([&] {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(Classify));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout(Classify), 0, 1, &globalDescriptorSetLayout, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout(Classify), 1, 1, &m_descriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout(Classify), 2, 1, &mesh.descriptorSet, 0, nullptr);
        vkCmdDispatchIndirect(cmd, mesh.indirectDispatchBuffer, 0);
        Barrier::computeWriteToRead(cmd);
    }, cmd, "classify");
}

void MeshUpdater::split(VkCommandBuffer cmd, const CBTMesh& mesh) const {
    m_Device->section([&] {
        const auto layout = m_compute.layout(Split);
        const auto neighborsBufferIndex = m_currentNeighborsBufferIdx;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(Split));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &m_descriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1, 1, &mesh.descriptorSet, 0, nullptr);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(neighborsBufferIndex), &neighborsBufferIndex);
        vkCmdDispatchIndirect(cmd, indirectBuffer, 0);
        Barrier::computeWriteToRead(cmd);
    }, cmd, "split");
}

void MeshUpdater::allocate(VkCommandBuffer cmd, const CBTMesh& mesh) const {
    m_Device->section([&] {
        const auto pipelineName = allocate_pipeline_name(mesh.cbtType);
        const auto layout = m_compute.layout(pipelineName);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(pipelineName));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &m_descriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1, 1, &mesh.descriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 2, 1, &mesh.cbtDescriptorSet, 0, nullptr);
        vkCmdDispatchIndirect(cmd, indirectBuffer, 0);
        Barrier::computeWriteToRead(cmd);
    }, cmd, "allocate");
}

void MeshUpdater::copy_neighbors(VkCommandBuffer cmd, const CBTMesh& mesh) const {
    const auto& currentNeighborsBuffer = mesh.neighborsBuffers[m_currentNeighborsBufferIdx];
    const auto& nextNeighborsBuffer = mesh.neighborsBuffers[m_nextNeighborsBufferIdx];
    const VkBufferCopy copy{ 0, 0, currentNeighborsBuffer.size };

    Barrier::computeWriteToTransferRead(cmd);
    Barrier::computeReadToTransferWrite(cmd);
    vkCmdCopyBuffer(cmd, currentNeighborsBuffer, nextNeighborsBuffer, 1, &copy);
    Barrier::transferWriteToComputeRead(cmd);
}

void MeshUpdater::bisect(VkCommandBuffer cmd, VkDescriptorSet globalDescriptorSetLayout, const CBTMesh& mesh) const {
    m_Device->section([&] {
        const auto pipelineName = bisect_pipeline_name(mesh.cbtType);
        const auto layout = m_compute.layout(pipelineName);
        const NeighborBufferIndices neighborBufferIndices{ m_currentNeighborsBufferIdx, m_nextNeighborsBufferIdx };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(pipelineName));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &globalDescriptorSetLayout, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1, 1, &m_descriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 2, 1, &mesh.descriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 3, 1, &mesh.cbtDescriptorSet, 0, nullptr);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(neighborBufferIndices), &neighborBufferIndices);
        vkCmdDispatchIndirect(cmd, indirectBuffer, 0);
        Barrier::computeWriteToRead(cmd);
    }, cmd, "bisect");
}

void MeshUpdater::propagate_bisect(VkCommandBuffer cmd, const CBTMesh &mesh) const {

    m_Device->section([&] {
        const auto pipelineName = PropagateBisect;
        const auto layout = m_compute.layout(pipelineName);
        const NeighborBufferIndices neighborBufferIndices{ m_currentNeighborsBufferIdx, m_nextNeighborsBufferIdx };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(pipelineName));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &m_descriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1, 1, &mesh.descriptorSet, 0, nullptr);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(neighborBufferIndices), &neighborBufferIndices);
        vkCmdDispatchIndirect(cmd, indirectBuffer, 0);
        Barrier::computeWriteToRead(cmd);
    }, cmd, "propagate_bisect");
}

void MeshUpdater::prepare_simplify(VkCommandBuffer cmd, VkDescriptorSet globalDescriptorSetLayout, const CBTMesh& mesh) const {
    m_Device->section([&] {
        const auto pipelineName = PrepareSimplify;
        const auto layout = m_compute.layout(pipelineName);
        const NeighborBufferIndices neighborBufferIndices{ m_currentNeighborsBufferIdx, m_nextNeighborsBufferIdx };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(pipelineName));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &globalDescriptorSetLayout, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1, 1, &m_descriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 2, 1, &mesh.descriptorSet, 0, nullptr);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(neighborBufferIndices), &neighborBufferIndices);
        vkCmdDispatchIndirect(cmd, indirectBuffer, sizeof(VkDispatchIndirectCommand));
        Barrier::computeWriteToRead(cmd);
    }, cmd, "prepare_simplify");
}

void MeshUpdater::simplify(VkCommandBuffer cmd, VkDescriptorSet globalDescriptorSetLayout, const CBTMesh& mesh) const {
    m_Device->section([&] {
        const auto pipelineName = simplify_pipeline_name(mesh.cbtType);
        const auto layout = m_compute.layout(pipelineName);
        const NeighborBufferIndices neighborBufferIndices{ m_currentNeighborsBufferIdx, m_nextNeighborsBufferIdx };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(pipelineName));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &globalDescriptorSetLayout, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1, 1, &m_descriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 2, 1, &mesh.descriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 3, 1, &mesh.cbtDescriptorSet, 0, nullptr);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(neighborBufferIndices), &neighborBufferIndices);
        vkCmdDispatchIndirect(cmd, indirectBuffer, 0);
        Barrier::computeWriteToRead(cmd);
    }, cmd, "simplify");
}

void MeshUpdater::propagate_simplify(VkCommandBuffer cmd, const CBTMesh& mesh) const {
    m_Device->section([&] {
        const auto pipelineName = PropagateSimplify;
        const auto layout = m_compute.layout(pipelineName);
        const NeighborBufferIndices neighborBufferIndices{ m_currentNeighborsBufferIdx, m_nextNeighborsBufferIdx };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(pipelineName));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &m_descriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1, 1, &mesh.descriptorSet, 0, nullptr);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(neighborBufferIndices), &neighborBufferIndices);
        vkCmdDispatchIndirect(cmd, indirectBuffer, sizeof(VkDispatchIndirectCommand));
        Barrier::computeWriteToRead(cmd);
    }, cmd, "propagate_simplify");
}

void MeshUpdater::reduce(VkCommandBuffer cmd, const CBTMesh& mesh) const {
    m_Device->section([&] {
        const auto prepassPipelineName = reduce_prepass_pipeline_name(mesh.cbtType);
        auto layout = m_compute.layout(prepassPipelineName);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(prepassPipelineName));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &mesh.cbtDescriptorSet, 0, nullptr);
        vkCmdDispatch(cmd, mesh.gpuCBT.lastLevelSize / (4 * WorkgroupSize), 1, 1);
        Barrier::computeWriteToRead(cmd);

        const auto firstPassPipelineName = reduce_first_pass_pipeline_name(mesh.cbtType);
        layout = m_compute.layout(firstPassPipelineName);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(firstPassPipelineName));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &mesh.cbtDescriptorSet, 0, nullptr);
        vkCmdDispatch(cmd, 8, 1, 1);
        Barrier::computeWriteToRead(cmd);

        const auto secondPassPipelineName = reduce_second_pass_pipeline_name(mesh.cbtType);
        layout = m_compute.layout(secondPassPipelineName);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(secondPassPipelineName));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &mesh.cbtDescriptorSet, 0, nullptr);
        vkCmdDispatch(cmd, 1, 1, 1);
        Barrier::computeWriteToRead(cmd);
    }, cmd, "reduce");
}

void MeshUpdater::prepare_indirection(VkCommandBuffer cmd, VkDescriptorSet meshDescriptorSet, int32_t bufferIndex, uint32_t gx, const std::string& section) const {
    m_Device->section([&] {
        const auto layout = m_compute.layout(PrepareIndirect);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(PrepareIndirect));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &m_descriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1, 1, &meshDescriptorSet, 0, nullptr);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(bufferIndex), &bufferIndex);
        vkCmdDispatch(cmd, gx, 1, 1);
        Barrier::computeWriteToDrawIndirect(cmd);
    }, cmd, fmt::format("prepare_indirect_{}", section));
}

void MeshUpdater::prepare_indirection(VkCommandBuffer cmd, const Planet& planet) const {
    m_Device->section([&] {
        const auto& mesh = planet.m_CBTMesh;
        const auto numGroups = (mesh.totalNumElements + WorkgroupSize - 1) / WorkgroupSize;

        auto layout = m_compute.layout(BisectorIndexation);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(BisectorIndexation));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &mesh.descriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 1, 1, &m_globalDescriptorSet, 0, nullptr);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(mesh.currentNeighborsBufferIdx), &mesh.currentNeighborsBufferIdx);
        vkCmdDispatch(cmd, numGroups, 1, 1);

        Barrier::computeWriteToRead(cmd);

        layout = m_compute.layout(PrepareBisectorIndirect);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(PrepareBisectorIndirect));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &mesh.descriptorSet, 0, nullptr);
        vkCmdDispatch(cmd, 1, 1, 1);

        Barrier::computeWriteToDrawIndirect(cmd);
    }, cmd, "prepare_indirect_draw_dispatch");
}

void MeshUpdater::enqueue_buffer_dump(VkCommandBuffer cmd, const char* name, const VulkanBuffer& sourceBuffer,
                                      VkDeviceSize elementSize, BufferDumpFormat format) {
    PendingBufferDump dump;
    dump.name = name;
    dump.readbackBuffer = m_Device->createBuffer(
        VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sourceBuffer.size,
        fmt::format("{}_frame_{}_readback", name, m_pendingBufferDumpFrameIndex));
    dump.elementSize = elementSize;
    dump.format = format;

    const VkBufferCopy copy{0, 0, sourceBuffer.size};
    vkCmdCopyBuffer(cmd, sourceBuffer, dump.readbackBuffer, 1, &copy);
    Barrier::transferWriteToHostRead(cmd, dump.readbackBuffer);
    m_pendingBufferDumps.push_back(std::move(dump));
}

void MeshUpdater::capture_frame_buffer_dumps(VkCommandBuffer cmd, const CBTMesh& mesh, const BaseMesh& baseMesh,
                                             const VulkanBuffer& lebMatrixBuffer, uint32_t frameIndex) {
    if (!m_pendingBufferDumps.empty())
        return;

    m_pendingBufferDumpFrameIndex = frameIndex;
    m_pendingNeighborsBufferIdx = mesh.currentNeighborsBufferIdx;
    Barrier::computeWriteToTransferRead(cmd);

    enqueue_buffer_dump(cmd, "earth.base.vertexBuffer", baseMesh.vertexBuffer, sizeof(glm::vec3), BufferDumpFormat::Float3);
    enqueue_buffer_dump(cmd, "earth.base.indexBuffer", baseMesh.indexBuffer, sizeof(glm::uvec3), BufferDumpFormat::UInt3);
    enqueue_buffer_dump(cmd, "earth.cbt.bufferArray0", mesh.gpuCBT.bufferArray[0], sizeof(uint32_t), BufferDumpFormat::UInt32);
    enqueue_buffer_dump(cmd, "earth.cbt.bufferArray1", mesh.gpuCBT.bufferArray[1], sizeof(uint64_t), BufferDumpFormat::UInt64);
    enqueue_buffer_dump(cmd, "earth.heapIDBuffer", mesh.heapIDBuffer, sizeof(uint64_t), BufferDumpFormat::UInt64);
    enqueue_buffer_dump(cmd, "earth.neighborsBuffers0", mesh.neighborsBuffers[0], sizeof(glm::uvec3), BufferDumpFormat::UInt3);
    enqueue_buffer_dump(cmd, "earth.neighborsBuffers1", mesh.neighborsBuffers[1], sizeof(glm::uvec3), BufferDumpFormat::UInt3);
    enqueue_buffer_dump(cmd, "earth.updateBuffer", mesh.updateBuffer, sizeof(cbt_large::BisectorData), BufferDumpFormat::Bisector);
    enqueue_buffer_dump(cmd, "earth.classificationBuffer", mesh.classificationBuffer, sizeof(uint32_t), BufferDumpFormat::UInt32);
    enqueue_buffer_dump(cmd, "earth.simplificationBuffer", mesh.simplificationBuffer, sizeof(uint32_t), BufferDumpFormat::UInt32);
    enqueue_buffer_dump(cmd, "earth.allocateBuffer", mesh.allocateBuffer, sizeof(uint32_t), BufferDumpFormat::UInt32);
    enqueue_buffer_dump(cmd, "earth.propagateBuffer", mesh.propagateBuffer, sizeof(uint32_t), BufferDumpFormat::UInt32);
    enqueue_buffer_dump(cmd, "earth.indirectDrawBuffer", mesh.indirectDrawBuffer, sizeof(uint32_t), BufferDumpFormat::UInt32);
    enqueue_buffer_dump(cmd, "earth.indirectDispatchBuffer", mesh.indirectDispatchBuffer, sizeof(uint32_t), BufferDumpFormat::UInt32);
    enqueue_buffer_dump(cmd, "earth.indexedBisectorBuffer", mesh.indexedBisectorBuffer, sizeof(uint32_t), BufferDumpFormat::UInt32);
    enqueue_buffer_dump(cmd, "earth.visibleIndexedBisectorBuffer", mesh.visibleIndexedBisectorBuffer, sizeof(uint32_t), BufferDumpFormat::UInt32);
    enqueue_buffer_dump(cmd, "earth.modifiedIndexedBisectorBuffer", mesh.modifiedIndexedBisectorBuffer, sizeof(uint32_t), BufferDumpFormat::UInt32);

    const auto lebVertexFormat = mesh.lebVertexBuffer.size == sizeof(glm::dvec3) * mesh.totalNumElements * 4
                                     ? BufferDumpFormat::Double3
                                     : BufferDumpFormat::Float3;
    enqueue_buffer_dump(cmd, "earth.lebVertexBuffer", mesh.lebVertexBuffer,
                        lebVertexFormat == BufferDumpFormat::Double3 ? sizeof(glm::dvec3) : sizeof(glm::vec3),
                        lebVertexFormat);
    enqueue_buffer_dump(cmd, "earth.currentVertexBuffer", mesh.currentVertexBuffer, sizeof(glm::vec3), BufferDumpFormat::Float3);
    enqueue_buffer_dump(cmd, "earth.currentDisplacementBuffer", mesh.currentDisplacementBuffer, sizeof(glm::vec3), BufferDumpFormat::Float3);
    enqueue_buffer_dump(cmd, "meshUpdater.indirectBuffer", indirectBuffer, sizeof(uint32_t), BufferDumpFormat::UInt32);
    enqueue_buffer_dump(cmd, "meshUpdater.memoryBuffer", memoryBuffer, sizeof(int32_t), BufferDumpFormat::Int32);
    enqueue_buffer_dump(cmd, "meshUpdater.validationBuffer", validationBuffer, sizeof(int32_t), BufferDumpFormat::Int32);
    enqueue_buffer_dump(cmd, "lebMatrixCache", lebMatrixBuffer, sizeof(glm::mat3), BufferDumpFormat::Float3x3);
}

void MeshUpdater::write_pending_frame_buffer_dumps(const std::string& projectDir) {
    if (m_pendingBufferDumps.empty())
        return;

    m_Device->wait();

    const auto logDir = std::filesystem::path(projectDir) / "buffer_logs" /
                        fmt::format("frame_{:06}", m_pendingBufferDumpFrameIndex);
    std::filesystem::create_directories(logDir);

    const auto formatName = [](BufferDumpFormat format) {
        switch (format) {
            case BufferDumpFormat::UInt32: return "uint32";
            case BufferDumpFormat::Int32: return "int32";
            case BufferDumpFormat::UInt64: return "uint64";
            case BufferDumpFormat::UInt3: return "uint3";
            case BufferDumpFormat::Float3: return "float3";
            case BufferDumpFormat::Double3: return "double3";
            case BufferDumpFormat::Bisector: return "BisectorData";
            case BufferDumpFormat::Float3x3: return "float3x3";
        }
        return "unknown";
    };

    std::ofstream manifest(logDir / "manifest.txt");
    manifest << "currentNeighborsBufferIdx = " << m_pendingNeighborsBufferIdx << '\n';

    for (auto& dump : m_pendingBufferDumps) {
        manifest << dump.name << ": bytes = " << dump.readbackBuffer.size << ", elementSize = " << dump.elementSize
                 << ", format = " << formatName(dump.format) << '\n';

        std::ofstream file(logDir / fmt::format("{}.txt", dump.name));
        file << std::setprecision(std::numeric_limits<double>::max_digits10);
        const auto* buffer = static_cast<const char*>(dump.readbackBuffer.map());
        const auto numValues = dump.readbackBuffer.size / dump.elementSize;

        switch (dump.format) {
            case BufferDumpFormat::UInt32: {
                const auto* values = reinterpret_cast<const uint32_t*>(buffer);
                for (VkDeviceSize idx = 0; idx < numValues; ++idx) file << '[' << idx << "] = " << values[idx] << '\n';
                break;
            }
            case BufferDumpFormat::Int32: {
                const auto* values = reinterpret_cast<const int32_t*>(buffer);
                for (VkDeviceSize idx = 0; idx < numValues; ++idx) file << '[' << idx << "] = " << values[idx] << '\n';
                break;
            }
            case BufferDumpFormat::UInt64: {
                const auto* values = reinterpret_cast<const uint64_t*>(buffer);
                for (VkDeviceSize idx = 0; idx < numValues; ++idx) file << '[' << idx << "] = " << values[idx] << '\n';
                break;
            }
            case BufferDumpFormat::UInt3: {
                const auto* values = reinterpret_cast<const glm::uvec3*>(buffer);
                for (VkDeviceSize idx = 0; idx < numValues; ++idx)
                    file << '[' << idx << "] = { " << values[idx].x << ", " << values[idx].y << ", " << values[idx].z << " }\n";
                break;
            }
            case BufferDumpFormat::Float3: {
                const auto* values = reinterpret_cast<const glm::vec3*>(buffer);
                for (VkDeviceSize idx = 0; idx < numValues; ++idx)
                    file << '[' << idx << "] = { " << values[idx].x << ", " << values[idx].y << ", " << values[idx].z << " }\n";
                break;
            }
            case BufferDumpFormat::Double3: {
                const auto* values = reinterpret_cast<const glm::dvec3*>(buffer);
                for (VkDeviceSize idx = 0; idx < numValues; ++idx)
                    file << '[' << idx << "] = { " << values[idx].x << ", " << values[idx].y << ", " << values[idx].z << " }\n";
                break;
            }
            case BufferDumpFormat::Bisector: {
                const auto* values = reinterpret_cast<const cbt_large::BisectorData*>(buffer);
                for (VkDeviceSize idx = 0; idx < numValues; ++idx)
                    file << '[' << idx << "] = { subdivisionPattern = " << values[idx].subdivisionPattern
                         << ", indices = { " << values[idx].indices.x << ", " << values[idx].indices.y << ", "
                         << values[idx].indices.z << " }, problematicNeighbor = " << values[idx].problematicNeighbor
                         << ", bisectorState = " << values[idx].bisectorState << ", flags = " << values[idx].flags
                         << ", propagationID = " << values[idx].propagationID << " }\n";
                break;
            }
            case BufferDumpFormat::Float3x3: {
                const auto* values = reinterpret_cast<const glm::mat3*>(buffer);
                for (VkDeviceSize idx = 0; idx < numValues; ++idx)
                    for (uint32_t row = 0; row < 3; ++row)
                        for (uint32_t col = 0; col < 3; ++col)
                            file << '[' << idx << "][" << row << " * " << col << "] = " << values[idx][col][row] << '\n';
                break;
            }
        }

        dump.readbackBuffer.unmap();
    }

    m_pendingBufferDumps.clear();
}
