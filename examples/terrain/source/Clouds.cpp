#include "Clouds.hpp"
#include "Barrier.hpp"
#include "AppContext.hpp"
#include <imgui.h>

Clouds::Clouds(Context &context, AtmosphereModel::Descriptor atmDescriptor)
    : m_context{&context}
    , m_atmosphereDescriptor{atmDescriptor} {}

void Clouds::init() {
//    initQuery();
    initUniforms();
    createCloudShape();
    createDescriptorSetLayout();
    updateDescriptorSet();
    createRenderPipelines();
}

void Clouds::newFrame() {
    m_uniforms.cpu->viewProjection = context().viewProjection;
    m_uniforms.cpu->mouse = context().mouse;
    m_uniforms.cpu->cameraPosition = camera().position();
}

void Clouds::render(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.layout.handle, 0, m_sets.size(), m_sets.data(), 0, VK_NULL_HANDLE);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void Clouds::controls(bool show) {
    if(!show) return;

    ImGui::Begin("Clouds");
    ImGui::SetWindowSize({0, 0});
    ImGui::SliderFloat("coverage", &m_uniforms.cpu->coverage, 0, 1);
    ImGui::SliderFloat("type", &m_uniforms.cpu->cloudType, 0, 1);
    ImGui::SliderFloat("Precipitation", &m_uniforms.cpu->precipitation, 0, 1);
    ImGui::SliderFloat("Scale", &m_uniforms.cpu->scale, 1, 100);
    ImGui::SliderFloat("Wind speed", &m_uniforms.cpu->windSpeed, 0, 1);
    ImGui::End();
}

float Clouds::printPerfStats() {
    return 0;
}

Context &Clouds::context() {
    return *m_context;
}

void Clouds::initQuery() {
    profiler().addQuery(m_query);
}

void Clouds::createCloudShape() {
    static uint lSize = 256;
    static uint hSize = 128;
    textures::createNoTransition(device(), m_shape.lowFrequency, VK_IMAGE_TYPE_3D, VK_FORMAT_R16G16B16A16_SFLOAT, glm::uvec3{lSize});
    textures::createNoTransition(device(), m_shape.highFrequency, VK_IMAGE_TYPE_3D, VK_FORMAT_R16G16B16A16_SFLOAT, glm::uvec3{hSize});

    struct {
        uint lfSize;
        uint hfSize;
        uint lowFrequencyNoisesIndex;
        uint highFrequencyNoisesIndex;
    } constants{ lSize, hSize, bindlessDescriptor().reserveImageSlots(1), bindlessDescriptor().reserveImageSlots(1) };

    ComputePipelines compute{ &device(), {{
       .name = "cloud_shape",
       .shadePath = FileManager::resource("cloud_shape.comp.spv"),
       .layouts = { &bindlessDescriptorSetLayout() },
       .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants)} }
    }}};

    compute.createPipelines();

    bindlessDescriptor().update({ &m_shape.lowFrequency, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  constants.lowFrequencyNoisesIndex, VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &m_shape.highFrequency, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, constants.highFrequencyNoisesIndex, VK_IMAGE_LAYOUT_GENERAL });

    device().graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
       Barriers::push(m_shape.lowFrequency.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
       Barriers::push(m_shape.highFrequency.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
       Barriers::flush(commandBuffer);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("cloud_shape"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("cloud_shape"), 0, 1, &bindlessDescriptor().descriptorSet, 0, 0);
        vkCmdPushConstants(commandBuffer, compute.layout("cloud_shape"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);

        uint gs = lSize/8;
        vkCmdDispatch(commandBuffer, gs, gs, gs);

        Barriers::push(m_shape.lowFrequency.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Barriers::push(m_shape.highFrequency.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Barriers::flush(commandBuffer);

    });

    bindlessDescriptor().update({ .texture = &m_shape.lowFrequency , .index = m_uniforms.cpu->lowFrequencyTexIndex });
    bindlessDescriptor().update({ .texture = &m_shape.highFrequency, .index = m_uniforms.cpu->highFrequencyTexIndex });
}

void Clouds::initUniforms() {
    UniformData initialValues{};
    initialValues.lowFrequencyTexIndex = bindlessDescriptor().reserveTextureSlots(1);
    initialValues.highFrequencyTexIndex = bindlessDescriptor().reserveTextureSlots(1);

    m_uniforms.gpu = device().createCpuVisibleBuffer(&initialValues, sizeof(UniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    m_uniforms.cpu = reinterpret_cast<UniformData*>(m_uniforms.gpu.map());
}

void Clouds::createDescriptorSetLayout() {
    m_descriptorSetLayout =
        device().descriptorSetLayoutBuilder()
            .name("cloud_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
}

void Clouds::updateDescriptorSet() {
    auto sets = descriptorPool().allocate({ m_descriptorSetLayout});
    m_descriptorSet = sets[0];

    auto writes = initializers::writeDescriptorSets<1>();

    writes[0].dstSet = m_descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uniformInfo{ m_uniforms.gpu, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &uniformInfo;

    device().updateDescriptorSets(writes);

    m_sets[0] = m_descriptorSet;
    m_sets[1] = bindlessDescriptorSet();
    m_sets[2] = m_atmosphereDescriptor.set;
    m_sets[3] = context().subpassInputDescriptorSet;
}

void Clouds::createRenderPipelines() {
    m_render.pipeline =
        clipSpacePipelineBuilder()
            .shaderStage()
                .vertexShader(resource("cloud_render.vert.spv"))
                .fragmentShader(resource("cloud_render.frag.spv"))
            .depthStencilState()
                .disableDepthTest()
                .disableDepthWrite()
                .compareOpLessOrEqual()
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .depthAttachment(VK_FORMAT_D16_UNORM)
            .colorBlendState()
                .attachment().clear()
                    .enableBlend()
                    .colorBlendOp().add()
                    .alphaBlendOp().add()
                    .srcColorBlendFactor().srcAlpha()
                    .dstColorBlendFactor().oneMinusSrcAlpha()
                    .srcAlphaBlendFactor().one()
                    .dstAlphaBlendFactor().one()
                .add()
                .attachment().add()
            .layout()
                .addDescriptorSetLayout(m_descriptorSetLayout)
                .addDescriptorSetLayout(bindlessDescriptorSetLayout())
                .addDescriptorSetLayout(m_atmosphereDescriptor.setLayout)
                .addDescriptorSetLayout(context().subpassInputDescriptorSetLayout)
            .name("render_clouds")
        .build(m_render.layout);
}

void Clouds::endFrame() {
    m_uniforms.cpu->time += 0.0166666;
}
