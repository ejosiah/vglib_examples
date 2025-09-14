#include "Clouds.hpp"
#include "Barrier.hpp"

Clouds::Clouds(Context &context, AtmosphereModel::Descriptor atmDescriptor)
    : m_context{&context}
    , m_atmosphereDescriptor{atmDescriptor} {}

void Clouds::init() {
//    initQuery();
    initUniforms();
    createCloudShape();
}

void Clouds::newFrame() {

}

void Clouds::render(VkCommandBuffer commandBuffer) {

}

void Clouds::controls(bool show) {

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

    textures::createNoTransition(device(), m_shape.lowFrequency, VK_IMAGE_TYPE_3D, VK_FORMAT_R16G16B16A16_SFLOAT, glm::uvec3{256});
    textures::createNoTransition(device(), m_shape.highFrequency, VK_IMAGE_TYPE_3D, VK_FORMAT_R16G16B16A16_SFLOAT, glm::uvec3{128});

    struct {
        uint lfSize;
        uint hfSize;
        uint lowFrequencyNoisesIndex;
        uint highFrequencyNoisesIndex;
    } constants{ 256, 128, bindlessDescriptor().reserveImageSlots(1), bindlessDescriptor().reserveImageSlots(1) };

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
        vkCmdDispatch(commandBuffer, 32, 32, 32);

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
