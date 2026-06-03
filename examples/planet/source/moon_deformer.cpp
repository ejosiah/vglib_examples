#include "moon_deformer.hpp"

#include "Barrier.hpp"
#include "filemanager.hpp"

namespace {
    constexpr auto EvaluateDeformation = "EvaluateDeformation";
}

MoonDeformer::MoonDeformer(VulkanDevice& device)
    : m_Device(&device) {}

MoonDeformer::~MoonDeformer() = default;

void MoonDeformer::initialize() {
    createPipelines();
}

std::vector<PipelineMetaData> MoonDeformer::metadata() {
    return {
        {
            .name = EvaluateDeformation,
            .shadePath = FileManager::resource("moon_deformation.comp.spv"),
            .layouts = { &Planet::meshDescriptorSetLayout, &MoonMaterial::descriptorSetLayout },
        },
    };
}

void MoonDeformer::createPipelines() {
    m_compute = ComputePipelines{ m_Device, metadata() };
    m_compute.createPipelines();
}

void MoonDeformer::apply_deformation(VkCommandBuffer cmd, const Planet& planet, const MoonMaterial& moonMaterial) {
    m_Device->section([&] {
        const auto layout = m_compute.layout(EvaluateDeformation);
        const auto dispatchOffset = sizeof(VkDispatchIndirectCommand);
        const std::array descriptorSets{ planet.m_descriptorSet, moonMaterial.descriptor_set() };

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(EvaluateDeformation));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, descriptorSets.size(), descriptorSets.data(), 0, nullptr);
        vkCmdDispatchIndirect(cmd, planet.m_CBTMesh.indirectDispatchBuffer, dispatchOffset);
        Barrier::computeWriteToVertexDraw(cmd, { planet.m_CBTMesh.currentVertexBuffer, planet.m_CBTMesh.currentDisplacementBuffer });
        Barrier::computeWriteToVertexRead(cmd);
    }, cmd, "moon_deformation");
}
