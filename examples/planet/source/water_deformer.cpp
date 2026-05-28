#include "water_deformer.hpp"

#include "Barrier.hpp"
#include "filemanager.hpp"

namespace {
    constexpr auto EvaluateDeformation = "EvaluateDeformation";
}

WaterDeformer::WaterDeformer(VulkanDevice& device)
    : m_Device(&device) {}

WaterDeformer::~WaterDeformer() = default;

void WaterDeformer::initialize() {
    createPipelines();
}

std::vector<PipelineMetaData> WaterDeformer::metadata() {
    return {
        {
            .name = EvaluateDeformation,
            .shadePath = FileManager::resource("water_deformation.comp.spv"),
            .layouts = { &Planet::meshDescriptorSetLayout },
        },
    };
}

void WaterDeformer::createPipelines() {
    m_compute = ComputePipelines{ m_Device, metadata() };
    m_compute.createPipelines();
}

void WaterDeformer::apply_deformation(VkCommandBuffer cmd, const Planet& planet) {
    m_Device->section([&] {
        const auto layout = m_compute.layout(EvaluateDeformation);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(EvaluateDeformation));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &planet.m_descriptorSet, 0, nullptr);
        vkCmdDispatchIndirect(cmd, planet.m_CBTMesh.indirectDispatchBuffer, sizeof(VkDispatchIndirectCommand));
        Barrier::computeWriteToVertexDraw(cmd, { planet.m_CBTMesh.currentVertexBuffer });
    }, cmd, "water_deformation");
}
