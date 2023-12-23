#include "sky_dome.hpp"
#include "Vertex.h"
#include "GraphicsPipelineBuilder.hpp"
#include "primitives.h"

SkyDome::SkyDome(const VulkanDevice &device, const VulkanDescriptorPool &descriptorPool, const FileManager &fileManager,
                 VulkanRenderPass &renderPass, uint32_t width, uint32_t height)
        :m_device{&device}
        ,m_descriptorPool{&descriptorPool}
        , m_filemanager(&fileManager)
        , m_width{width}
        , m_height{ height }
        , m_renderPass{ &renderPass }{
    initUBO();
    initVertexBuffers();
    createDescriptorSetLayout();
    updateDescriptorSet();
    createPipelines();
}

void SkyDome::update(const SceneData &sceneData) {
    ubo->mvp = sceneData.camera.proj * sceneData.camera.view * glm::scale(glm::mat4(1), glm::vec3(domeHeight));
    ubo->eyes = sceneData.eyes;
    ubo->sun = sceneData.sun.position;
}

void SkyDome::render(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet,1> sets;
    sets[0] = descriptorSet;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyDome.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyDome.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, indexBuffer.sizeAs<uint32_t>(), 1, 0, 0, 0);
}

void SkyDome::renderUI(VkCommandBuffer commandBuffer) {

}

void SkyDome::resize(VulkanRenderPass &renderPass, uint32_t width, uint32_t height) {
    m_renderPass = &renderPass;
    m_width = width;
    m_height = height;
    createPipelines();
}

void SkyDome::initUBO() {
    uboBuffer = device().createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(UniformBufferObject), "terrain");
    ubo = reinterpret_cast<UniformBufferObject*>(uboBuffer.map());
}

void SkyDome::initVertexBuffers() {
    domeHeight = EARTH_RADIUS + CLOUD_MAX + 10 * km;
    auto hemisphere = primitives::sphere(1000, 1000, 1, glm::mat4(1), glm::vec4(0), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vertexBuffer = device().createDeviceLocalBuffer(hemisphere.vertices.data(), BYTE_SIZE(hemisphere.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    indexBuffer = device().createDeviceLocalBuffer(hemisphere.indices.data(), BYTE_SIZE(hemisphere.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void SkyDome::createDescriptorSetLayout() {
    descriptorSetLayout =
        device().descriptorSetLayoutBuilder()
            .name("sky_dome")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT)
        .createLayout();
}

void SkyDome::updateDescriptorSet() {
    auto sets = descriptorPool().allocate( { descriptorSetLayout });
    
    descriptorSet = sets[0];

    auto writes = initializers::writeDescriptorSets<1>();
    
    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uboInfo{ uboBuffer, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &uboInfo;

    device().updateDescriptorSets(writes);

}

void SkyDome::createPipelines() {
    //    @formatter:off
    auto builder = device().graphicsPipelineBuilder();
    skyDome.pipeline =
        builder
            .shaderStage()
                .vertexShader(resource("sky.vert.spv"))
                .fragmentShader(resource("sky.frag.spv"))
            .vertexInputState()
                .addVertexBindingDescriptions(Vertex::bindingDisc())
                .addVertexAttributeDescriptions(Vertex::attributeDisc())
            .inputAssemblyState()
                .triangles()
            .viewportState()
                .viewport()
                    .origin(0, 0)
                    .dimension(m_width, m_height)
                    .minDepth(0)
                    .maxDepth(1)
                .scissor()
                    .offset(0, 0)
                    .extent(m_width, m_height)
                .add()
                .rasterizationState()
                    .cullNone()
                    .frontFaceCounterClockwise()
                    .polygonModeFill()
                .multisampleState()
                    .rasterizationSamples(VK_SAMPLE_COUNT_1_BIT)
                .depthStencilState()
                    .enableDepthWrite()
                    .enableDepthTest()
                    .compareOpLess()
                    .minDepthBounds(0)
                    .maxDepthBounds(1)
                .colorBlendState()
                    .attachment()
                    .add()
                .layout()
                    .addDescriptorSetLayout(descriptorSetLayout)
                .renderPass(renderPass())
                .subpass(0)
                .name("sky_dome")
            .build(skyDome.layout);
    //    @formatter:on
}

std::string SkyDome::resource(const std::string &name) {
    auto res = m_filemanager->getFullPath(name);
    assert(res.has_value());
    return res->string();
}
