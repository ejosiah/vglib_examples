#include "mesh_updater.hpp"
#include "AppContext.hpp"
#include "planet.hpp"
#include "filemanager.hpp"
#include <array>
#include <cinttypes>

#include "Barrier.hpp"

namespace {
    constexpr auto ResetPipeline = "Reset";
    constexpr auto ClassifyPipeline = "Classify";

    VkDescriptorBufferInfo descriptor_buffer_info(const VulkanBuffer& buffer) { return { buffer, 0, VK_WHOLE_SIZE }; }

    void set_buffer_write(VkWriteDescriptorSet& write, uint32_t binding, const VkDescriptorBufferInfo* bufferInfo) {
        write.dstBinding = binding;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = bufferInfo;
    }
}

MeshUpdater::MeshUpdater(VulkanDevice &device, VulkanDescriptorSetLayout globalDescriptorSetLayout)
: m_Device(&device)
, m_globalDescriptorSetLayout(globalDescriptorSetLayout){}

void MeshUpdater::initialize() {
    indirectBuffer = m_Device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(uint32_t) * 9);
    memoryBuffer = m_Device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int32_t) * 2);
    validationBuffer = m_Device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int32_t) * 2);
    validationBufferRB = m_Device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(int32_t) * 2);
    occupancyBufferRB = m_Device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(uint32_t) * 2);

    createDescriptorSetLayout();
    updateDescriptorSet();
    createPipelines();
}

void MeshUpdater::update(VkCommandBuffer cmd, VkDescriptorSet globalDescriptorSet, VkDescriptorSet meshDescriptorSet, VkDescriptorSet cbtDescriptorSet) {
    m_Device->group([&] {
        reset_buffers(cmd, meshDescriptorSet);


    }, cmd, "Update Mesh");
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
        .createLayout();
}

void MeshUpdater::updateDescriptorSet() {
    if (m_descriptorSet == VK_NULL_HANDLE) m_descriptorSet = AppContext::descriptorPool().allocate({ m_descriptorSetLayout }).front();

    VkDescriptorBufferInfo memoryInfo = descriptor_buffer_info(memoryBuffer);
    VkDescriptorBufferInfo validationInfo = descriptor_buffer_info(validationBuffer);

    auto writes = initializers::writeDescriptorSets<2>(m_descriptorSet);
    set_buffer_write(writes[0], 0, &memoryInfo);
    set_buffer_write(writes[1], 1, &validationInfo);
    m_Device->updateDescriptorSets(writes);
}

std::vector<PipelineMetaData> MeshUpdater::metadata() {
    return {
        {
            .name = ResetPipeline,
            .shadePath = FileManager::resource("mesh_reset.comp.spv"),
            .layouts = { &m_descriptorSetLayout, &Planet::descriptorSetLayout, &Planet::cbtDescriptorSetLayout },
        },
        {
            .name = ClassifyPipeline,
            .shadePath = FileManager::resource("mesh_classify.comp.spv"),
            .layouts = { &m_globalDescriptorSetLayout, &m_descriptorSetLayout, &Planet::descriptorSetLayout },
        },
    };
}

void MeshUpdater::createPipelines() {
    m_compute = ComputePipelines{ m_Device, metadata() };
    m_compute.createPipelines();
}

void MeshUpdater::reset_buffers(VkCommandBuffer cmd, VkDescriptorSet meshDescriptorSet) {
    Barrier::fragmentReadToComputeWrite(cmd);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(ResetPipeline));
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout(ResetPipeline), 0, 1, &m_descriptorSet, 0, nullptr);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout(ResetPipeline), 1, 1, &meshDescriptorSet, 0, nullptr);
    vkCmdDispatch(cmd, 1, 1, 1);
    Barrier::computeWriteToRead(cmd);
}
