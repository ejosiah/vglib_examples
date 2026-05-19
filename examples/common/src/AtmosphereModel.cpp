#include "vista/AtmosphereModel.hpp"
#include "Barrier.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include <imgui.h>

AtmosphereModel::AtmosphereModel(Context &context)
: m_context{&context} {

}

void AtmosphereModel::init() {
    initQueries();
    initUniforms();
    createLoopUpTextures();
    createDescriptorSetLayout();
    updateDescriptorSet();
    createComputePipelines();
    createRenderPipelines();
}

void AtmosphereModel::initUniforms() {

    UniformData initialValues{};

    set(initialValues);
    initialValues.transmittanceTextureIndex = context().transmittanceTextureIndex;
    initialValues.multiScatteringTextureIndex = context().multiScatteringTextureIndex;
    initialValues.skyViewTextureIndex = context().skyViewTextureIndex;
    initialValues.arealPerspectiveTextureIndex = context().arealPerspectiveTextureIndex;

    initialValues.transmittanceImageIndex = bindlessDescriptor().reserveImageSlots(1);
    initialValues.multiScatteringImageIndex = bindlessDescriptor().reserveImageSlots(1);
    initialValues.skyViewImageIndex = bindlessDescriptor().reserveImageSlots(1);
    initialValues.arealPerspectiveImageIndex = bindlessDescriptor().reserveImageSlots(1);

    spdlog::info("Atmosphere ubo size; {}", sizeof(UniformData));
    m_uniforms.gpu = device().createCpuVisibleBuffer(&initialValues, sizeof(UniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    m_uniforms.cpu = reinterpret_cast<UniformData*>(m_uniforms.gpu.map());
}

void AtmosphereModel::set(UniformData& uniform) {
    uniform.cameraPosition = camera().position();
    uniform.sunDirection = context().lightDirection;
    uniform.inverseProjection = context().inverseProjection;
    uniform.inverseView = context().inverseView;

    uniform.solarIrradiance = params.solarIrradiance;
    uniform.sunAngularRadius = params.sunAngularRadius;
    uniform.bottomRadius = params.radius.bottom / params.lengthUnitInMeters;
    uniform.topRadius = uniform.bottomRadius + 100;

    uniform.rayleighScattering = params.rayleigh.scattering * params.lengthUnitInMeters;
    uniform.mieScattering = params.mie.scattering * params.lengthUnitInMeters;
    uniform.mieExtinction = params.mie.extinction * params. lengthUnitInMeters;
    uniform.mieAnisotropicFactor = params.mie.anisotropicFactor;

    uniform.ozoneExtinction = params.ozone.absorptionExtinction * params.lengthUnitInMeters;
    uniform.groundAlbedo = params.groundAlbedo;
    uniform.mu_s_min = params.mu_s_min;
    uniform.lengthUnitInMeters = params.lengthUnitInMeters;


    uniform.rayleighDensity[BOTTOM].width = 0;
    uniform.rayleighDensity[BOTTOM].exp_term = 1;
    uniform.rayleighDensity[BOTTOM].exp_scale = -km / params.rayleigh.height;
    uniform.rayleighDensity[BOTTOM].linear_term = 0;
    uniform.rayleighDensity[BOTTOM].constant_term = 0;

    uniform.mieDensity[BOTTOM].width = 0;
    uniform.mieDensity[BOTTOM].exp_term = 1;
    uniform.mieDensity[BOTTOM].exp_scale = -km / params.mie.height;
    uniform.mieDensity[BOTTOM].linear_term = 0;
    uniform.mieDensity[BOTTOM].constant_term = 0;

    uniform.ozone[BOTTOM].width = params.ozone.bottom.width / params.lengthUnitInMeters;
    uniform.ozone[BOTTOM].exp_term = 0;
    uniform.ozone[BOTTOM].exp_scale = 0;
    uniform.ozone[BOTTOM].linear_term = km / params.ozone.bottom.linearHeight;
    uniform.ozone[BOTTOM].constant_term =  params.ozone.bottom.constant;

    uniform.ozone[TOP].width = 0;
    uniform.ozone[TOP].exp_term = 0;
    uniform.ozone[TOP].exp_scale = 0;
    uniform.ozone[TOP].linear_term = -km / params.ozone.top.linearHeight;
    uniform.ozone[TOP].constant_term =  params.ozone.top.constant;
}

void AtmosphereModel::newFrame() {
    set(*m_uniforms.cpu);
}

void AtmosphereModel::preProcess(VkCommandBuffer commandBuffer) {
    computeTransmittanceLUT(commandBuffer);
    computeMultipleScatteringLUT(commandBuffer);
    computeSkyViewLUT(commandBuffer);
    computeArealPerspectiveLut(commandBuffer);
}

void AtmosphereModel::renderSkyView(VkCommandBuffer commandBuffer) {
    profiler().profile(queryIds[QUERY_SKY_VIEW_RENDER_ID], commandBuffer, [&] {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.skyView.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.skyView.layout.handle, 0, m_sets.size(), m_sets.data(), 0, VK_NULL_HANDLE);
        AppContext::renderClipSpaceQuad(commandBuffer);
    });
}

void AtmosphereModel::renderArealPerspective(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = m_sets[0];
    sets[1] = m_sets[1];
    sets[2] = context().subpassInputDescriptorSet;

    profiler().profile(queryIds[QUERY_AREAL_PERSPECTIVE_RENDER_ID], commandBuffer, [&] {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.arealPerspective.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.arealPerspective.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
        AppContext::renderClipSpaceQuad(commandBuffer);
    });

}

void AtmosphereModel::controls(bool show) {
    if(!show) return;

    ImGui::Begin("Atmosphere");
    ImGui::SetWindowSize({0, 0});
    controlsContent();
    ImGui::End();
}

void AtmosphereModel::controlsContent() {
    auto defaultParams = Atmosphere::Params{};
    static auto mieAbsorption = glm::max(glm::vec3(0), params.mie.extinction - params.mie.scattering);
    static auto mieScatteringLength = glm::length(defaultParams.mie.scattering) * km;
    static auto mieAbsorptionLength = glm::length(mieAbsorption) * km;
    static auto rayleighScattingLength = glm::length(defaultParams.rayleigh.scattering) * km;
    static auto ozoneAbsorptionLength = glm::length(defaultParams.ozone.absorptionExtinction) * km;

    ImGui::Checkbox("Areal perspective", &m_arealPerspectiveEnabled);
    ImGui::SliderFloat("Mie phase", &params.mie.anisotropicFactor, 0, 0.999);
    ImGui::SliderInt("Scatt Order", &params.numScatteringOrder, 2, 10);

    static auto mieScattering = params.mie.scattering * km/mieScatteringLength;
    ImGui::ColorEdit3("MieScattCoeff", glm::value_ptr(mieScattering));
    ImGui::SliderFloat("MieScattScale", &mieScatteringLength, 0.00001f, 0.1f, "%.5f");

    static auto mieAbsorptionColor = mieAbsorption * km/mieAbsorptionLength;
    ImGui::ColorEdit3("MieAbsorbCoeff", glm::value_ptr(mieAbsorptionColor));
    ImGui::SliderFloat("MieAbsorbScale", &mieAbsorptionLength, 0.00001f, 0.1f, "%.5f");

    static auto rayleighScattering = params.rayleigh.scattering * km/rayleighScattingLength;
    ImGui::ColorEdit3( "RayScattCoeff", glm::value_ptr(rayleighScattering));
    ImGui::SliderFloat("RayScattScale", &rayleighScattingLength, 0.00001f, 10.0f, "%.5f");

    static auto ozoneAbsorption = params.ozone.absorptionExtinction * km/ozoneAbsorptionLength;
    ImGui::ColorEdit3( "AbsorptiCoeff", glm::value_ptr(ozoneAbsorption));
    ImGui::SliderFloat("AbsorptiScale", &ozoneAbsorptionLength, 0.00001f, 10.0f, "%.5f");

    static auto planetRadius = params.radius.bottom / params.lengthUnitInMeters;
    static auto atmosphereHeight = (params.radius.top - params.radius.bottom) / params.lengthUnitInMeters;
    ImGui::SliderFloat("Planet radius", &planetRadius, 100.0f, 8000.0f);
    ImGui::SliderFloat("Atmos height", &atmosphereHeight, 10.0f, 150.0f);

    static auto mieScaleHeight = params.mie.height / params.lengthUnitInMeters;
    static auto rayleighHeight = params.rayleigh.height / params.lengthUnitInMeters;
    ImGui::SliderFloat("MieScaleHeight", &mieScaleHeight, 0.5f, 20.0f);
    ImGui::SliderFloat("RayleighScaleHeight", &rayleighHeight, 0.5f, 20.0f);

//    ImGui::ColorEdit3("Ground albedo", glm::value_ptr(params.groundAlbedo));

    if(ImGui::Button("reset")) {
        mieAbsorption = glm::max(glm::vec3(0), defaultParams.mie.extinction - defaultParams.mie.scattering);
        mieScatteringLength = glm::length(defaultParams.mie.scattering) * km;
        mieAbsorptionLength = glm::length(mieAbsorption) * km;
        rayleighScattingLength = glm::length(defaultParams.rayleigh.scattering) * km;
        ozoneAbsorptionLength = glm::length(defaultParams.ozone.absorptionExtinction) * km;

        mieScattering = defaultParams.mie.scattering * km/mieScatteringLength;
        mieAbsorptionColor = mieAbsorption * km/mieAbsorptionLength;
        rayleighScattering = defaultParams.rayleigh.scattering * km/rayleighScattingLength;
        ozoneAbsorption = defaultParams.ozone.absorptionExtinction * km/ozoneAbsorptionLength;
        planetRadius = defaultParams.radius.bottom / defaultParams.lengthUnitInMeters;
        atmosphereHeight = (defaultParams.radius.top - defaultParams.radius.bottom) / defaultParams.lengthUnitInMeters;
        mieScaleHeight = defaultParams.mie.height / defaultParams.lengthUnitInMeters;
        rayleighHeight = defaultParams.rayleigh.height / defaultParams.lengthUnitInMeters;
    }

    params.mie.scattering = mieScattering * mieScatteringLength/km;
    mieAbsorption = mieAbsorptionColor * mieAbsorptionLength/km;
    params.mie.extinction = params.mie.scattering + mieAbsorption;
    params.rayleigh.scattering = rayleighScattering * rayleighScattingLength/km;
    params.ozone.absorptionExtinction = ozoneAbsorption * ozoneAbsorptionLength/km;
    params.radius.bottom = planetRadius * km;
    params.radius.top = (planetRadius + atmosphereHeight) * km;
    params.mie.height = mieScaleHeight * km;
    params.rayleigh.height = rayleighHeight * km;
}

Context &AtmosphereModel::context() {
    return *m_context;
}

void AtmosphereModel::createLoopUpTextures() {
    m_lut.transmittance.sampler =
    m_lut.multiScattering.sampler =
    m_lut.skyView.sampler =
    m_lut.arealPerspective.sampler = context().edgeClampSampler;

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
}

void AtmosphereModel::createComputePipelines() {
    m_compute = ComputePipelines{ m_context->device, metadata() };
    m_compute.createPipelines();
}

void AtmosphereModel::createRenderPipelines() {
    const auto w = context().screenWidth;
    const auto h = context().screenHeight;
    const auto scissorWidth = static_cast<int32_t>(w);
    const auto scissorHeight = static_cast<int32_t>(h);

    m_render.skyView.pipeline =
        clipSpacePipelineBuilder()
            .shaderStage()
                .vertexShader(resource("vista_atmosphere_render_sky_view.vert.spv"))
                .fragmentShader(resource("vista_atmosphere_render_sky_view.frag.spv"))
            .depthStencilState()
                .compareOpLessOrEqual()
            .viewportState().clear()
                .viewport()
                    .origin(0, 0)
                    .dimension(w, h)
                .scissor()
                    .offset(0, 0)
                    .extent(scissorWidth, scissorHeight)
                .add()
            .dynamicState()
                .viewport()
                .scissor()
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

    std::array<uint32_t, 2> colors{ 0u, 1u };
    uint32_t depthIndex = 2;
    VkRenderingInputAttachmentIndexInfoKHR inputAttachments{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INPUT_ATTACHMENT_INDEX_INFO_KHR,
            .colorAttachmentCount = COUNT(colors),
            .pColorAttachmentInputIndices = colors.data(),
            .pDepthInputAttachmentIndex = &depthIndex
    };

    auto builder = clipSpacePipelineBuilder() ;
    auto info =
        builder
            .shaderStage()
                .vertexShader(resource("quad.vert.spv"))
                .fragmentShader(resource("vista_atmosphere_areal_perspective.frag.spv"))
            .depthStencilState()
                .disableDepthTest()
                .disableDepthWrite()
            .viewportState().clear()
                .viewport()
                    .origin(0, 0)
                    .dimension(w, h)
                .scissor()
                    .offset(0, 0)
                    .extent(scissorWidth, scissorHeight)
                .add()
            .dynamicState()
                .viewport()
                .scissor()
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .depthAttachment(VK_FORMAT_D16_UNORM)
            .colorBlendState()
                .attachment().clear()
                    .enableBlend()
                    .colorBlendOp().add()
                    .alphaBlendOp().add()
                    .srcColorBlendFactor().one()
                    .dstColorBlendFactor().srcAlpha()
                    .srcAlphaBlendFactor().one()
                    .dstAlphaBlendFactor().one()
                .add()
                .attachment().add()
            .layout()
                .addDescriptorSetLayout(m_descriptor.setLayout)
                .addDescriptorSetLayout(bindlessDescriptorSetLayout())
                .addDescriptorSetLayout(context().subpassInputDescriptorSetLayout)
            .name("render_areal_perspective")
        .createInfo();

    auto dynamicRenderPass = const_cast<VkPipelineRenderingCreateInfo*>(reinterpret_cast<const VkPipelineRenderingCreateInfo*>(info.pNext));
    dynamicRenderPass->pNext = &inputAttachments;

    m_render.arealPerspective.layout = builder.pipelineLayout();
    m_render.arealPerspective.pipeline = device().createGraphicsPipeline(info);
    device().setName<VK_OBJECT_TYPE_PIPELINE>("render_areal_perspective", m_render.arealPerspective.pipeline.handle);
    device().setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("render_areal_perspective", m_render.arealPerspective.layout.handle);

}

void AtmosphereModel::computeTransmittanceLUT(VkCommandBuffer commandBuffer) {
    const auto gx = (TRANSMITTANCE_TEXTURE_WIDTH + 7)/8;
    const auto gy = (TRANSMITTANCE_TEXTURE_HEIGHT + 3)/4;
    profiler().profile(queryIds[QUERY_TRANSMISSION_LUT_ID], commandBuffer, [&]{
        prepareForWriting(commandBuffer, m_lut.transmittance.image);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("compute_transmittance"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("compute_transmittance") , 0, m_sets.size(), m_sets.data(), 0, VK_NULL_HANDLE);
        vkCmdDispatch(commandBuffer, gx, gy, 1);
        prepareForReading(commandBuffer, m_lut.transmittance.image);
    });
}

void AtmosphereModel::computeMultipleScatteringLUT(VkCommandBuffer commandBuffer) {
    profiler().profile(queryIds[QUERY_MULTIPLE_SCATTERING_LUT_ID], commandBuffer, [&] {
        prepareForWriting(commandBuffer, m_lut.multiScattering.image);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("compute_multiscattering"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_compute.layout("compute_multiscattering"), 0, m_sets.size(), m_sets.data(), 0,
                                VK_NULL_HANDLE);
        vkCmdDispatch(commandBuffer, 32, 32, 1);
        prepareForReading(commandBuffer, m_lut.multiScattering.image);
    });
}

void AtmosphereModel::computeSkyViewLUT(VkCommandBuffer commandBuffer) {
    const auto gx = (SKY_VIEW_TEXTURE_WIDTH + 7)/8;
    const auto gy = (SKY_VIEW_TEXTURE_HEIGHT + 7)/8;

    profiler().profile(queryIds[QUERY_SKY_VIEW_LUT_ID], commandBuffer, [&] {
        prepareForWriting(commandBuffer, m_lut.skyView.image);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("compute_skyview"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("compute_skyview"), 0,
                                m_sets.size(), m_sets.data(), 0, VK_NULL_HANDLE);
        vkCmdDispatch(commandBuffer, gx, gy, 1);
        prepareForReading(commandBuffer, m_lut.skyView.image);
    });
}

void AtmosphereModel::computeArealPerspectiveLut(VkCommandBuffer commandBuffer) {
    profiler().profile(queryIds[QUERY_AREAL_PERSPECTIVE_LUT_ID], commandBuffer, [&] {
        prepareForWriting(commandBuffer, m_lut.arealPerspective.image);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("compute_areal_perspective"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,m_compute.layout("compute_areal_perspective"), 0, m_sets.size(), m_sets.data(), 0,VK_NULL_HANDLE);
        vkCmdDispatch(commandBuffer, 1, 32, 32);
        prepareForReading(commandBuffer, m_lut.arealPerspective.image);
    });
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
            .shadePath = FileManager::resource("vista_atmosphere_transmission_lut.comp.spv"),
            .layouts = { &m_descriptor.setLayout, &bindlessDescriptorSetLayout() },
        },
        {
            .name = "compute_multiscattering",
            .shadePath = FileManager::resource("vista_atmosphere_multiscattering_lut.comp.spv"),
            .layouts = { &m_descriptor.setLayout, &bindlessDescriptorSetLayout() },
        },
        {
            .name = "compute_skyview",
            .shadePath = FileManager::resource("vista_atmosphere_sky_view_lut.comp.spv"),
            .layouts = { &m_descriptor.setLayout, &bindlessDescriptorSetLayout() },
        },
        {
            .name = "compute_areal_perspective",
            .shadePath = FileManager::resource("vista_atmosphere_areal_perspective_lut.comp.spv"),
            .layouts = { &m_descriptor.setLayout, &bindlessDescriptorSetLayout() },
        },
    };
}

AtmosphereModel::Descriptor AtmosphereModel::descriptor() const {
    return m_descriptor;
}

bool AtmosphereModel::arealPerspectiveEnabled() const {
    return m_arealPerspectiveEnabled;
}

void AtmosphereModel::initQueries() {
    for(auto query : queryIds) {
        profiler().addQuery(query);
    }
}

float AtmosphereModel::printPerfStats() {
    const auto toMillis = 1e-6f;
    auto total = 0.0f;

    if (ImGui::TreeNode("Atmosphere")) {
        // Leaf flags so they render as rows without opening/closing arrows
        ImGuiTreeNodeFlags leaf = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        for(auto name : queryIds) {
            auto duration = profiler().queries[name].movingAverage.value * toMillis;
            ImGui::TreeNodeEx(name.c_str(), leaf, "%s: %f ms", name.c_str(), duration);
            total += duration;
        }
        ImGui::TreeNodeEx("total", leaf, "total: %f ms", total);
        ImGui::TreePop();
    }
    return total;
}
