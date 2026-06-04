#include "moon_renderer.hpp"

#include "AppContext.hpp"
#include "descriptor_utils.hpp"
#include "filemanager.hpp"

#include <array>

MoonRenderer::MoonRenderer(const Params& params)
    : m_device(&params.device)
    , m_planet(&params.planet)
    , m_material(&params.material)
    , m_globalDescriptorSetLayout(params.globalDescriptorSetLayout) {}

void MoonRenderer::initialize() {
    createLayoutDescriptorSet();
    updateDescriptorSetLayout();
    createPipeline();
}

void MoonRenderer::render(VkCommandBuffer commandBuffer, VkDescriptorSet& globalDescriptorSet, bool isVisible) {
    if (isVisible) {
        render_mesh(commandBuffer, globalDescriptorSet);
    } else {
        render_impostor(commandBuffer, globalDescriptorSet);
    }
}

void MoonRenderer::render_mesh(VkCommandBuffer commandBuffer, VkDescriptorSet& globalDescriptorSet) {
    auto& atmosphere = AppContext::atmosphere();
    const std::array sets{
        globalDescriptorSet,
        m_descriptorSet,
        atmosphere.info.descriptorSet,
        m_material->descriptor_set(),
    };

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout.handle, 0, COUNT(sets), sets.data(), 0, nullptr);
    vkCmdDrawIndirect(commandBuffer, m_planet->m_CBTMesh.indirectDrawBuffer, 0, 1, sizeof(VkDrawIndirectCommand));
}

void MoonRenderer::render_impostor(VkCommandBuffer commandBuffer, VkDescriptorSet& globalDescriptorSet) {
    auto& atmosphere = AppContext::atmosphere();
    const std::array sets{
        globalDescriptorSet,
        m_descriptorSet,
        atmosphere.info.descriptorSet,
        m_material->descriptor_set(),
    };

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_impostorPipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_impostorLayout.handle, 0, COUNT(sets), sets.data(), 0, nullptr);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
}

void MoonRenderer::createPipeline() {
    m_pipeline =
        AppContext::prototypes().cloneGraphicsPipeline()
            .shaderStage()
                .vertexShader(FileManager::resource("moon_render.vert.spv"))
                .geometryShader(FileManager::resource("moon_render.geom.spv"))
                .fragmentShader(FileManager::resource("moon_render.frag.spv"))
            .vertexInputState().clear()
            .rasterizationState()
                .cullNone()
            .layout()
                .addDescriptorSetLayout(m_globalDescriptorSetLayout)
                .addDescriptorSetLayout(m_descriptorSetLayout)
                .addDescriptorSetLayout(AppContext::uniformDescriptorSet())
                .addDescriptorSetLayout(MoonMaterial::descriptorSetLayout)
            .name("moon_renderer")
        .build(m_layout);

    m_impostorPipeline =
        AppContext::prototypes().cloneGraphicsPipeline()
            .shaderStage()
                .vertexShader(FileManager::resource("impostor.vert.spv"))
                .fragmentShader(FileManager::resource("moon_impostor.frag.spv"))
            .vertexInputState().clear()
            .inputAssemblyState()
                .triangles()
            .rasterizationState()
                .cullNone()
            .depthStencilState()
                .compareOpLessOrEqual()
            .layout().clear()
                .addDescriptorSetLayout(m_globalDescriptorSetLayout)
                .addDescriptorSetLayout(m_descriptorSetLayout)
                .addDescriptorSetLayout(AppContext::uniformDescriptorSet())
                .addDescriptorSetLayout(MoonMaterial::descriptorSetLayout)
            .name("moon_impostor_renderer")
        .build(m_impostorLayout);
}

void MoonRenderer::createLayoutDescriptorSet() {
    m_descriptorSetLayout =
        m_device->descriptorSetLayoutBuilder()
            .name("moon_descriptor_set_layout")
            .binding(0) // m_updateCB
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
            .binding(1) // planet
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
            .binding(2) // m_CBTMesh.currentVertexBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
            .binding(3) // m_CBTMesh.currentDisplacementBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
            .binding(4) // m_CBTMesh.indexedBisectorBuffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
        .createLayout();
}

void MoonRenderer::updateDescriptorSetLayout() {
    auto sets = AppContext::descriptorPool().allocate({ m_descriptorSetLayout });
    m_descriptorSet = sets[0];

    m_device->setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("moon_descriptor_set", m_descriptorSet);

    VkDescriptorBufferInfo updateInfo = descriptor_buffer_info(m_planet->m_UpdateCB.gpu);
    VkDescriptorBufferInfo planetInfo = descriptor_buffer_info(m_planet->m_PlanetCB);
    VkDescriptorBufferInfo currentVertexInfo = descriptor_buffer_info(m_planet->m_CBTMesh.currentVertexBuffer);
    VkDescriptorBufferInfo currentDisplacementInfo = descriptor_buffer_info(m_planet->m_CBTMesh.currentDisplacementBuffer);
    VkDescriptorBufferInfo indexedBisectorInfo = descriptor_buffer_info(m_planet->m_CBTMesh.indexedBisectorBuffer);

    auto writes = initializers::writeDescriptorSets<5>(m_descriptorSet);
    set_buffer_write(writes[0], 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &updateInfo);
    set_buffer_write(writes[1], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &planetInfo);
    set_buffer_write(writes[2], 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &currentVertexInfo);
    set_buffer_write(writes[3], 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &currentDisplacementInfo);
    set_buffer_write(writes[4], 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &indexedBisectorInfo);
    m_device->updateDescriptorSets(writes);
}
