#include "FFTOcean2.hpp"
#include "Barrier.hpp"
#include "filemanager.hpp"
#include "Vertex.h"
#include "GraphicsPipelineBuilder.hpp"
#include "AppContext.hpp"


FFTOcean2::FFTOcean2(VulkanDevice &device, VulkanDescriptorPool &descriptorPool, BindlessDescriptor &bindlessDescriptor,
                     Prototypes& prototypes, BaseCameraController& camera, uint width, uint height)
        : SubdivisionGrid(device, descriptorPool, bindlessDescriptor, "ocean", glm::vec2{width, height})
        , m_prototypes{&prototypes}
        , m_camera{&camera}
{
    m_topViewResolution = glm::vec4{0, 0, width, height};
}

void FFTOcean2::init() {
    initUniforms();
    SubdivisionGrid::init();
}

PipelineMetaData FFTOcean2::subdivisionMetadata() {
    auto bindlessDescriptorSetLayout = const_cast<VulkanDescriptorSetLayout*>(m_bindlessDescriptor->descriptorSetLayout);
    return {
            .name = "ocean_subdivide",
            .shadePath = FileManager::resource("ocean_subdivision.comp.spv"),
            .layouts = { &m_subdGridDescriptorSetLayout, bindlessDescriptorSetLayout, &m_uniformsDescriptorSetLayout },
            .ranges = {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int)}}
    };
}

void FFTOcean2::subdivide(VkCommandBuffer commandBuffer, int pingPong) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = m_sets[0];
    sets[1] = m_sets[1];
    sets[2] = m_uniformsDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("ocean_subdivide"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("ocean_subdivide"), 0, COUNT(sets), sets.data(), 0,nullptr);
    vkCmdPushConstants(commandBuffer, m_compute.layout("ocean_subdivide"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &pingPong);
    vkCmdDispatchIndirect(commandBuffer, m_dispatchBuffer, 0);
    Barrier::computeWriteToRead(commandBuffer);
}

void FFTOcean2::createPipelines() {
    SubdivisionGrid::createPipelines();

    m_render.pipeline =
        m_prototypes->cloneGraphicsPipeline()
            .shaderStage()
                .vertexShader(FileManager::resource("ocean_render.vert.spv"))
                .fragmentShader(FileManager::resource("ocean_render.frag.spv"))
            .vertexInputState().clear()
                .addVertexBindingDescription(0, sizeof(glm::vec2), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32_SFLOAT, 0)
            .inputAssemblyState()
                .triangles()
            .rasterizationState()
                .cullNone()
                .polygonModeFill()
            .layout().clear()
                .addDescriptorSetLayout(*m_layouts[0])
                .addDescriptorSetLayout(*m_layouts[1])
                .addDescriptorSetLayout(m_uniformsDescriptorSetLayout)
                .name("ocean_render")
        .build(m_render.layout);
}

void FFTOcean2::render(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = m_sets[0];
    sets[1] = m_sets[1];
    sets[2] = m_uniformsDescriptorSet;

    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.layout.handle, 0, COUNT(sets), sets.data(), 0,nullptr);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertices.buffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, m_indexes, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexedIndirect(commandBuffer, m_drawBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
}

void FFTOcean2::preProcess(VkCommandBuffer commandBuffer) {
    update(commandBuffer);
}

void FFTOcean2::newFrame() {
    auto view = m_camera->camera.view;
    auto projection = m_camera->camera.proj;

    glm::mat4 model = m_uniforms.cpu->modelMatrix;
    auto mvp = projection * view * model;
    m_uniforms.cpu->modelViewMatrix = view * model;
    m_uniforms.cpu->viewMatrix = view;
    m_uniforms.cpu->cameraMatrix = glm::inverse(view);
    m_uniforms.cpu->viewProjectionMatrix = projection * view;
    m_uniforms.cpu->modelViewProjectionMatrix = mvp;
    m_uniforms.cpu->lodFactor = computeLodFactor();
    m_uniforms.cpu->dmapFactor = m_options.dmapScale;
    m_uniforms.cpu->minLodVariance = std::sqrt(m_options.minLodStdev / 64.f / m_options.dmapScale);

    static Frustum frustum;
    Frustum::extractFrustum(frustum, mvp);
    std::memcpy(m_uniforms.cpu->frustumPlanes.data(), frustum.cp.data(), BYTE_SIZE(frustum.cp));
}

void FFTOcean2::endFrame() {

}

void FFTOcean2::initUniforms() {
    const float width = m_dimensions.width;
    const float height = m_dimensions.height;
    glm::mat4 model = glm::mat4{1};
    model = glm::scale(model, {width, 1, height});
    model = glm::rotate(model, -glm::half_pi<float>(), {1, 0, 0});
    model = glm::translate(model, {-0.5f, -0.5f, 0.0f});

    defaultValues.modelMatrix = model;
    defaultValues.heightMapIndex = m_heightMapIndex;
    defaultValues.normalMapIndex = m_normalIndex;

    m_uniforms.gpu = m_device->createCpuVisibleBuffer(&defaultValues, sizeof(UniformData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    m_uniforms.cpu = reinterpret_cast<UniformData*>(m_uniforms.gpu.map());
    m_device->setName<VK_OBJECT_TYPE_BUFFER>("ocean_uniforms", m_uniforms.gpu.buffer);
}

void FFTOcean2::createDescriptorSetLayout() {
    SubdivisionGrid::createDescriptorSetLayout();

    m_uniformsDescriptorSetLayout =
        m_device->descriptorSetLayoutBuilder()
            .name("ocean_uniforms_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
}

void FFTOcean2::updateDescriptorSets() {
    SubdivisionGrid::updateDescriptorSets();

    auto sets = m_descriptorPool->allocate(
            {m_uniformsDescriptorSetLayout} );
    m_uniformsDescriptorSet = sets[0];

    auto writes = initializers::writeDescriptorSets<1>();\

    writes[0].dstSet = m_uniformsDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uniformsInfo{ m_uniforms.gpu, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &uniformsInfo;

    m_device->updateDescriptorSets(writes);
}

float FFTOcean2::computeLodFactor() {
    const auto h = m_screenResolution.y;
    const auto gpuSubd = m_options.gpuSubDivisions;
    const auto fov = m_camera->fov;
    float tmp = 2.0f * tan(glm::radians(fov) / 2.0f) / h * (1 << gpuSubd) * m_options.primitivePixelLengthTarget;
    auto rtVal =  -2.0f * std::log2(tmp) + 2.0f;
    return rtVal;
}
