#include "earth_renderer.hpp"

#include "AppContext.hpp"
#include "filemanager.hpp"

namespace {
    VkDescriptorBufferInfo descriptor_buffer_info(const VulkanBuffer& buffer) { return { buffer, 0, VK_WHOLE_SIZE }; }

    void set_buffer_write(VkWriteDescriptorSet& write, uint32_t binding, VkDescriptorType descriptorType, const VkDescriptorBufferInfo* bufferInfo, uint32_t descriptorCount = 1) {
        write.dstBinding = binding;
        write.descriptorType = descriptorType;
        write.descriptorCount = descriptorCount;
        write.pBufferInfo = bufferInfo;
    }
}

EarthRenderer::EarthRenderer(const Params &params)
    : m_device(&params.device)
    , m_planet(&params.planet)
    , m_globalDescriptorSetLayout(params.globalDescriptorSetLayout){}

void EarthRenderer::initialize() {
    createLayoutDescriptorSet();
    updateDescriptorSetLayout();
    createPipeline();
}

void EarthRenderer::render(VkCommandBuffer commandBuffer, const BaseCameraController& camera, VkDescriptorSet& globalDescriptorSet) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout.handle, 0, 1, &globalDescriptorSet, 0, nullptr);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout.handle, 1, 1, &m_descriptorSet, 0, nullptr);
    camera.push(commandBuffer, m_layout);
    vkCmdDrawIndirect(commandBuffer, m_planet->m_CBTMesh.indirectDrawBuffer, 0, 1, sizeof(VkDrawIndirectCommand));
}

void EarthRenderer::createPipeline() {
    m_pipeline =
        AppContext::prototypes().cloneGraphicsPipeline()
            .shaderStage()
                .vertexShader(FileManager::resource("earth_render.vert.spv"))
                .geometryShader(FileManager::resource("earth_render.geom.spv"))
                .fragmentShader(FileManager::resource("earth_render.frag.spv"))
            .vertexInputState().clear()
            .rasterizationState()
                .cullNone()
                // .frontFaceClockwise()
                // .polygonModeLine()
            .layout()
                .addDescriptorSetLayout(m_globalDescriptorSetLayout)
                .addDescriptorSetLayout(m_descriptorSetLayout)
            .name("earth_renderer")
        .build(m_layout);

}

void EarthRenderer::createLayoutDescriptorSet() {
    m_descriptorSetLayout =
    m_device->descriptorSetLayoutBuilder()
        .name("earth_descriptor_set_layout")
        .binding(0)  // m_updateCB
            .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .descriptorCount(1)
            .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
        .binding(1)  // plant
            .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .descriptorCount(1)
            .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
        .binding(2) //  m_CBTMesh.currentVertexBuffer
            .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .descriptorCount(1)
            .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
        .binding(3)  // m_CBTMesh.currentDisplacementBuffer
            .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .descriptorCount(1)
            .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
        .binding(4) //  m_CBTMesh.indexedBisectorBuffer
            .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
            .descriptorCount(1)
            .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
    .createLayout();
}

void EarthRenderer::updateDescriptorSetLayout() {
    auto sets = AppContext::descriptorPool().allocate({ m_descriptorSetLayout });
    m_descriptorSet = sets[0];

    m_device->setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("earth_descriptor_set", m_descriptorSet);

    auto writes = initializers::writeDescriptorSets<5>(m_descriptorSet);
    VkDescriptorBufferInfo updateInfo = descriptor_buffer_info(m_planet->m_UpdateCB.gpu);
    VkDescriptorBufferInfo planetInfo = descriptor_buffer_info(m_planet->m_PlanetCB);
    VkDescriptorBufferInfo currentVertexInfo = descriptor_buffer_info(m_planet->m_CBTMesh.currentVertexBuffer);
    VkDescriptorBufferInfo currentDisplacementInfo = descriptor_buffer_info(m_planet->m_CBTMesh.currentDisplacementBuffer);
    VkDescriptorBufferInfo indexedBisectorInfo = descriptor_buffer_info(m_planet->m_CBTMesh.indexedBisectorBuffer);


    writes = initializers::writeDescriptorSets<5>(m_descriptorSet);
    set_buffer_write(writes[0], 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &updateInfo);
    set_buffer_write(writes[1], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &planetInfo);
    set_buffer_write(writes[2], 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &currentVertexInfo);
    set_buffer_write(writes[3], 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &currentDisplacementInfo);
    set_buffer_write(writes[4], 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &indexedBisectorInfo);
    m_device->updateDescriptorSets(writes);

}
