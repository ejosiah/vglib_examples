#include "AtmosphereModel.hpp"
#include "atmosphere/Atmosphere.hpp"
#include "Barrier.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

AtmosphereModel::AtmosphereModel(Context &context)
: m_context{&context} {

}

void AtmosphereModel::init() {
    initUniforms();
    createLoopUpTextures();
    createDescriptorSetLayout();
    updateDescriptorSet();
    createComputePipelines();
    createRenderPipelines();

    device().graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        preProcess(commandBuffer);
    });
}

void AtmosphereModel::initUniforms() {

    UniformData initialValues{};
    
    Atmosphere::Params params{};
    initialValues.cameraPosition = camera().position();
    initialValues.sunDirection = context().lightDirection;
    initialValues.inverseProjection = context().inverseProjection;
    initialValues.inverseView = context().inverseView;

    initialValues.solarIrradiance = params.solarIrradiance;
    initialValues.sunAngularRadius = params.sunAngularRadius;
    initialValues.bottomRadius = params.radius.bottom / params.lengthUnitInMeters;
    initialValues.topRadius = initialValues.bottomRadius + 100;

    initialValues.rayleighScattering = params.rayleigh.scattering * params.lengthUnitInMeters;
    initialValues.mieScattering = params.mie.scattering * params.lengthUnitInMeters;
    initialValues.mieExtinction = params.mie.extinction * params. lengthUnitInMeters;
    initialValues.mieAnisotropicFactor = params.mie.anisotropicFactor;

    initialValues.ozoneExtinction = params.ozone.absorptionExtinction * params.lengthUnitInMeters;
    initialValues.groundAlbedo = params.groundAlbedo;
    initialValues.mu_s_min = params.mu_s_min;
    initialValues.lengthUnitInMeters = params.lengthUnitInMeters;


    initialValues.rayleighDensity[BOTTOM].width = 0;
    initialValues.rayleighDensity[BOTTOM].exp_term = 1;
    initialValues.rayleighDensity[BOTTOM].exp_scale = -km / params.rayleigh.height;
    initialValues.rayleighDensity[BOTTOM].linear_term = 0;
    initialValues.rayleighDensity[BOTTOM].constant_term = 0;

    initialValues.mieDensity[BOTTOM].width = 0;
    initialValues.mieDensity[BOTTOM].exp_term = 1;
    initialValues.mieDensity[BOTTOM].exp_scale = -km / params.mie.height;
    initialValues.mieDensity[BOTTOM].linear_term = 0;
    initialValues.mieDensity[BOTTOM].constant_term = 0;

    initialValues.ozone[BOTTOM].width = params.ozone.bottom.width / params.lengthUnitInMeters;
    initialValues.ozone[BOTTOM].exp_term = 0;
    initialValues.ozone[BOTTOM].exp_scale = 0;
    initialValues.ozone[BOTTOM].linear_term = km / params.ozone.bottom.linearHeight;
    initialValues.ozone[BOTTOM].constant_term =  params.ozone.bottom.constant;

    initialValues.ozone[TOP].width = 0;
    initialValues.ozone[TOP].exp_term = 0;
    initialValues.ozone[TOP].exp_scale = 0;
    initialValues.ozone[TOP].linear_term = -km / params.ozone.top.linearHeight;
    initialValues.ozone[TOP].constant_term =  params.ozone.top.constant;

    initialValues.transmittanceTextureIndex = context().transmittanceTextureIndex;
    initialValues.multiScatteringTextureIndex = context().multiScatteringTextureIndex;
    initialValues.skyViewTextureIndex = context().skyViewTextureIndex;
    initialValues.arealPerspectiveTextureIndex = context().arealPerspectiveTextureIndex;

    initialValues.transmittanceImageIndex = bindlessDescriptor().reserveImageSlots(1);
    initialValues.multiScatteringImageIndex = bindlessDescriptor().reserveImageSlots(1);
    initialValues.skyViewImageIndex = bindlessDescriptor().reserveImageSlots(1);
    initialValues.arealPerspectiveImageIndex = bindlessDescriptor().reserveImageSlots(1);

    initialValues.brunetonScatteringTextureIndex = context().brunetonScatteringTextureIndex;
    initialValues.brunetonSingleScatteringTextureIndex = context().brunetonSingleScatteringTextureIndex;
    initialValues.brunetonIrradianceTextureIndex = context().brunetonIrradianceTextureIndex;

    initialValues.radianceTextureIndex = context().radianceTextureIndex;
    initialValues.positionTextureIndex = context().positionTextureIndex;
    initialValues.depthTextureIndex = context().depthTextureIndex;

    spdlog::info("Atmosphere ubo size; {}", sizeof(UniformData));
    m_uniforms.gpu = device().createCpuVisibleBuffer(&initialValues, sizeof(UniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    m_uniforms.cpu = reinterpret_cast<UniformData*>(m_uniforms.gpu.map());
}

void AtmosphereModel::newFrame() {
    m_uniforms.cpu->cameraPosition = camera().position();
    m_uniforms.cpu->sunDirection = context().lightDirection;
    m_uniforms.cpu->inverseProjection = context().inverseProjection;
    m_uniforms.cpu->inverseView = context().inverseView;
    m_useBruneton = context().useBruneton;
    m_uniforms.cpu->exposure = context().exposure;
}

void AtmosphereModel::preProcess(VkCommandBuffer commandBuffer) {
    computeTransmittanceLUT(commandBuffer);
    computeMultipleScatteringLUT(commandBuffer);
    computeSkyViewLUT(commandBuffer);
    computeArealPerspectiveLut(commandBuffer);
}

void AtmosphereModel::render(VkCommandBuffer commandBuffer) {

}


void AtmosphereModel::renderSkyView(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.skyView.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.skyView.layout.handle, 0, m_sets.size(), m_sets.data(), 0, VK_NULL_HANDLE);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void AtmosphereModel::renderSkyViewBruneton(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.bruneton.skyView.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.bruneton.skyView.layout.handle, 0, m_sets.size(), m_sets.data(), 0, VK_NULL_HANDLE);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void AtmosphereModel::renderArealPerspective(VkCommandBuffer commandBuffer) {


}

void AtmosphereModel::renderArealPerspectiveBruneton(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = m_sets[0];
    sets[1] = m_sets[1];
    sets[2] = context().subpassInputDescriptorSet;

//    std::array<uint32_t, 2> colors{ 0u, 1u };
//    uint32_t depthIndex = 2;
//    VkRenderingInputAttachmentIndexInfoKHR idx{
//        .sType = VK_STRUCTURE_TYPE_RENDERING_INPUT_ATTACHMENT_INDEX_INFO_KHR,
//        .colorAttachmentCount = COUNT(colors),
//        .pColorAttachmentInputIndices = colors.data(),
//        .pDepthInputAttachmentIndex = &depthIndex
//    };
//
//    vkCmdSetRenderingInputAttachmentIndicesKHR(commandBuffer, &idx);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.bruneton.arealPerspective.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.bruneton.arealPerspective.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void AtmosphereModel::controls() {

}

Context &AtmosphereModel::context() {
    return *m_context;
}

void AtmosphereModel::createLoopUpTextures() {
    textures::create(device(), m_lut.transmittance, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {TRANSMITTANCE_TEXTURE_WIDTH, TRANSMITTANCE_TEXTURE_HEIGHT, 1});
    textures::create(device(), m_lut.multiScattering, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {MULTI_SCATTERING_TEXTURE_WIDTH, MULTI_SCATTERING_TEXTURE_HEIGHT, 1});
    textures::create(device(), m_lut.skyView, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {SKY_VIEW_TEXTURE_WIDTH, SKY_VIEW_TEXTURE_HEIGHT, 1});
    textures::create(device(), m_lut.arealPerspective, VK_IMAGE_TYPE_3D, VK_FORMAT_R16G16B16A16_SFLOAT, {AREAL_PERSPECTIVE_TEXTURE_WIDTH, AREAL_PERSPECTIVE_TEXTURE_HEIGHT, AREAL_PERSPECTIVE_TEXTURE_DEPTH});

    bindlessDescriptor().update({ &m_lut.transmittance, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_uniforms.cpu->transmittanceTextureIndex });
    bindlessDescriptor().update({ &m_lut.multiScattering, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_uniforms.cpu->multiScatteringTextureIndex });
    bindlessDescriptor().update({ &m_lut.skyView, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_uniforms.cpu->skyViewTextureIndex });
    bindlessDescriptor().update({ &m_lut.arealPerspective, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_uniforms.cpu->arealPerspectiveTextureIndex });

    bindlessDescriptor().update({ &m_lut.transmittance, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_uniforms.cpu->transmittanceImageIndex, VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &m_lut.multiScattering, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_uniforms.cpu->multiScatteringImageIndex, VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &m_lut.skyView, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_uniforms.cpu->skyViewImageIndex, VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &m_lut.arealPerspective, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_uniforms.cpu->arealPerspectiveImageIndex, VK_IMAGE_LAYOUT_GENERAL });

    auto& atmosphere = AppContext::atmosphere().descriptor;
    bindlessDescriptor().update({ &atmosphere.irradianceLut, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context().brunetonIrradianceTextureIndex });
    bindlessDescriptor().update({ &atmosphere.scatteringLUT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context().brunetonScatteringTextureIndex});
    bindlessDescriptor().update({ &atmosphere.scatteringLUT, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context().brunetonSingleScatteringTextureIndex });
}

void AtmosphereModel::createComputePipelines() {
    m_compute = ComputePipelines{ m_context->device, metadata() };
    m_compute.createPipelines();
}

void AtmosphereModel::createRenderPipelines() {
    m_render.skyView.pipeline =
        clipSpacePipelineBuilder()
            .shaderStage()
                .vertexShader(resource("atmosphere_render_sky_view.vert.spv"))
                .fragmentShader(resource("atmosphere_render_sky_view.frag.spv"))
            .depthStencilState()
                .compareOpLessOrEqual()
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .depthAttachment(VK_FORMAT_D16_UNORM)
            .colorBlendState()
                .attachments(2)
            .layout()
                .addDescriptorSetLayout(m_descriptor.setLayout)
                .addDescriptorSetLayout(bindlessDescriptorSetLayout())
            .name("render_sky_view")
        .build(m_render.skyView.layout);

    m_render.bruneton.skyView.pipeline =
        clipSpacePipelineBuilder()
            .shaderStage()
                .vertexShader(resource("atmosphere_render_sky_view.vert.spv"))
                .fragmentShader(resource("atmosphere_render_sky_view_bruneton.frag.spv"))
            .depthStencilState()
                .compareOpLessOrEqual()
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .depthAttachment(VK_FORMAT_D16_UNORM)
            .colorBlendState()
                .attachments(2)
            .layout()
                .addDescriptorSetLayout(m_descriptor.setLayout)
                .addDescriptorSetLayout(bindlessDescriptorSetLayout())
            .name("render_sky_view_bruneton")
        .build(m_render.bruneton.skyView.layout);


//    std::array<uint32_t, 2> colors{ 0u, 1u };
//    uint32_t depthIndex = 2;
//    VkRenderingInputAttachmentIndexInfoKHR inputAttachments{
//            .sType = VK_STRUCTURE_TYPE_RENDERING_INPUT_ATTACHMENT_INDEX_INFO_KHR,
//            .colorAttachmentCount = COUNT(colors),
//            .pColorAttachmentInputIndices = colors.data(),
//            .pDepthInputAttachmentIndex = &depthIndex
//    };
//
//    auto builder = clipSpacePipelineBuilder() ;
//    auto info =
//        builder
//            .shaderStage()
//                .vertexShader(resource("quad.vert.spv"))
//                .fragmentShader(resource("atmosphere_areal_perspective_bruneton.frag.spv"))
//            .dynamicRenderPass()
//                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
//                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
//                .depthAttachment(VK_FORMAT_D16_UNORM)
//                .colorBlendState()
//                .attachments(2)
//            .layout()
//                .addDescriptorSetLayout(m_descriptor.setLayout)
//                .addDescriptorSetLayout(bindlessDescriptorSetLayout())
//                .addDescriptorSetLayout(context().subpassInputDescriptorSetLayout)
//            .name("render_areal_perspective_bruneton")
//        .createInfo();
//
//    auto dynamicRenderPass = const_cast<VkPipelineRenderingCreateInfo*>(reinterpret_cast<const VkPipelineRenderingCreateInfo*>(info.pNext));
//    dynamicRenderPass->pNext = &inputAttachments;
//
//    m_render.bruneton.arealPerspective.layout = builder.pipelineLayout();
//    m_render.bruneton.arealPerspective.pipeline = device().createGraphicsPipeline(info);

    m_render.bruneton.arealPerspective.pipeline =
        clipSpacePipelineBuilder()
            .shaderStage()
                .vertexShader(resource("quad.vert.spv"))
                .fragmentShader(resource("atmosphere_areal_perspective_bruneton.frag.spv"))
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .depthAttachment(VK_FORMAT_D16_UNORM)
            .colorBlendState()
                .attachments(2)
            .layout()
                .addDescriptorSetLayout(m_descriptor.setLayout)
                .addDescriptorSetLayout(bindlessDescriptorSetLayout())
                .addDescriptorSetLayout(context().subpassInputDescriptorSetLayout)
            .name("render_areal_perspective_bruneton")
        .build(m_render.bruneton.arealPerspective.layout);

}

void AtmosphereModel::computeTransmittanceLUT(VkCommandBuffer commandBuffer) {
    const auto gx = (TRANSMITTANCE_TEXTURE_WIDTH + 7)/8;
    const auto gy = (TRANSMITTANCE_TEXTURE_HEIGHT + 3)/4;

    prepareForWriting(commandBuffer, m_lut.transmittance.image);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("compute_transmittance"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("compute_transmittance") , 0, m_sets.size(), m_sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gx, gy, 1);
    prepareForReading(commandBuffer, m_lut.transmittance.image);
}

void AtmosphereModel::computeMultipleScatteringLUT(VkCommandBuffer commandBuffer) {
    prepareForWriting(commandBuffer, m_lut.multiScattering.image);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("compute_multiscattering"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("compute_multiscattering") , 0, m_sets.size(), m_sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, 32, 32, 1);
    prepareForReading(commandBuffer, m_lut.multiScattering.image);
}

void AtmosphereModel::computeSkyViewLUT(VkCommandBuffer commandBuffer) {
    const auto gx = (SKY_VIEW_TEXTURE_WIDTH + 7)/8;
    const auto gy = (SKY_VIEW_TEXTURE_HEIGHT + 7)/8;

    prepareForWriting(commandBuffer, m_lut.skyView.image);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("compute_skyview"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("compute_skyview") , 0, m_sets.size(), m_sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gx, gy, 1);
    prepareForReading(commandBuffer, m_lut.skyView.image);
}

void AtmosphereModel::computeArealPerspectiveLut(VkCommandBuffer commandBuffer) {
    prepareForWriting(commandBuffer, m_lut.arealPerspective.image);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("compute_areal_perspective"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("compute_areal_perspective") , 0, m_sets.size(), m_sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, 1, 32, 32);
    prepareForReading(commandBuffer, m_lut.arealPerspective.image);
}


void AtmosphereModel::createDescriptorSetLayout() {
    m_descriptor.setLayout =
        device().descriptorSetLayoutBuilder()
            .name("atmosphere_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
}

void AtmosphereModel::updateDescriptorSet() {
    auto sets = descriptorPool().allocate({ m_descriptor.setLayout});
    m_descriptor.set = sets[0];

    auto writes = initializers::writeDescriptorSets<1>();

    writes[0].dstSet = m_descriptor.set;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uniformInfo{ m_uniforms.gpu, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &uniformInfo;

    device().updateDescriptorSets(writes);

    m_sets[0] = m_descriptor.set;
    m_sets[1] = bindlessDescriptorSet();
}

void AtmosphereModel::prepareForWriting(VkCommandBuffer commandBuffer, const VulkanImage &image) {
    Barriers::pushAndFlush(commandBuffer, image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
}

void AtmosphereModel::prepareForReading(VkCommandBuffer commandBuffer, const VulkanImage &image) {
    Barriers::pushAndFlush(commandBuffer, image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  VK_ACCESS_SHADER_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

std::vector<PipelineMetaData> AtmosphereModel::metadata() {
    return {
        {
            .name = "compute_transmittance",
            .shadePath = FileManager::resource("atmosphere_transmission_lut.comp.spv"),
            .layouts = { &m_descriptor.setLayout, &bindlessDescriptorSetLayout() },
        },
        {
            .name = "compute_multiscattering",
            .shadePath = FileManager::resource("atmosphere_multiscattering_lut.comp.spv"),
            .layouts = { &m_descriptor.setLayout, &bindlessDescriptorSetLayout() },
        },
        {
            .name = "compute_skyview",
            .shadePath = FileManager::resource("atmosphere_sky_view_lut.comp.spv"),
            .layouts = { &m_descriptor.setLayout, &bindlessDescriptorSetLayout() },
        },
        {
            .name = "compute_areal_perspective",
            .shadePath = FileManager::resource("atmosphere_areal_perspective_lut.comp.spv"),
            .layouts = { &m_descriptor.setLayout, &bindlessDescriptorSetLayout() },
        },
    };
}

AtmosphereModel::Descriptor AtmosphereModel::descriptor() const {
    return m_descriptor;
}

void AtmosphereModel::useBruneton(bool flag) {
    m_useBruneton = flag;
}
