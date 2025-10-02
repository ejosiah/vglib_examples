#include "LebGrid.hpp"
#include "filemanager.hpp"
#include "Barrier.hpp"
#include "GraphicsPipelineBuilder.hpp"

LebGrid::LebGrid(VulkanDevice &device, VulkanDescriptorPool &descriptorPool, BindlessDescriptor &bindlessDescriptor, Prototypes& prototypes, uint width, uint height)
: SubdivisionGrid(device, descriptorPool, bindlessDescriptor, "leb_eval", glm::vec4{0, 0, width, height})
, m_prototypes{&prototypes} {}

PipelineMetaData LebGrid::subdivisionMetadata() {
    return {
            .name = "leb_subdivide",
            .shadePath = FileManager::resource("leb_subdivide.comp.spv"),
            .layouts = m_layouts,
            .ranges = {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int)}}
    };
}

void LebGrid::subdivide(VkCommandBuffer commandBuffer, int pingPong) {

    pingPong = m_pingPong;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("leb_subdivide"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("leb_subdivide"), 0, COUNT(m_sets), m_sets.data(), 0,nullptr);
    vkCmdPushConstants(commandBuffer, m_compute.layout("leb_subdivide"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &pingPong);
    vkCmdDispatchIndirect(commandBuffer, m_dispatchBuffer, 0);
    Barrier::computeWriteToRead(commandBuffer);
}

void LebGrid::createPipelines() {
    SubdivisionGrid::createPipelines();

    m_render.pipeline =
        m_prototypes->cloneGraphicsPipeline()
            .shaderStage()
                .vertexShader(FileManager::resource("render.vert.spv"))
                .geometryShader(FileManager::resource("render.geom.spv"))
                .fragmentShader(FileManager::resource("flat.frag.spv"))
            .vertexInputState().clear()
                .addVertexBindingDescription(0, sizeof(glm::vec2), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32_SFLOAT, 0)
            .inputAssemblyState()
                .triangles()
            .rasterizationState()
                .cullNone()
                .polygonModeLine()
            .layout().clear()
                .addDescriptorSetLayout(*m_layouts[0])
                .addDescriptorSetLayout(*m_layouts[1])
            .name("leb_render")
        .build(m_render.layout);
}

void LebGrid::render(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.layout.handle, 0, COUNT(m_sets), m_sets.data(), 0,nullptr);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertices.buffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, m_indexes, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexedIndirect(commandBuffer, m_drawBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
}
