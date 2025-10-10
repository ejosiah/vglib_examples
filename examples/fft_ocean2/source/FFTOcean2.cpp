#include "FFTOcean2.hpp"
#include "Barrier.hpp"
#include "filemanager.hpp"
#include "Vertex.h"
#include "GraphicsPipelineBuilder.hpp"
#include "AppContext.hpp"
#include <imgui.h>

FFTOcean2::FFTOcean2(VulkanDevice &device, VulkanDescriptorPool &descriptorPool, BindlessDescriptor &bindlessDescriptor,
                     Prototypes& prototypes, BaseCameraController& camera, uint width, uint height)
        : SubdivisionGrid(device, descriptorPool, bindlessDescriptor, "ocean", glm::vec2{width, height}, 0, nullptr)
        , m_prototypes{&prototypes}
        , m_camera{&camera}
{}

void FFTOcean2::init() {
    createSimTextures();
    initUniforms();
    SubdivisionGrid::init();
    generateGaussianNoise();
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

void FFTOcean2::generateGaussianNoise() {
    m_device->graphicsCommandPool().oneTimeCommand([this](auto commandBuffer){
        generateGaussianNoise(commandBuffer);
    });
}

void FFTOcean2::generateGaussianNoise(VkCommandBuffer commandBuffer) {
    const auto gc = glm::uvec2{tileSize/32u};
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("generate_gaussian_distribution"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("generate_gaussian_distribution"), 0, 1, &m_descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gc.x, gc.y, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void FFTOcean2::generateSpectralComponents(VkCommandBuffer commandBuffer) {
    const auto gc = glm::uvec2{tileSize/32u};
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("spectral_components"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("spectral_components"), 0, 1, &m_descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, m_compute.layout("spectral_components"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_controls), &m_controls);
    vkCmdDispatch(commandBuffer, gc.x, gc.y, tileCount);
    Barrier::computeWriteToRead(commandBuffer);
}

void FFTOcean2::generateSpectralHeightField(VkCommandBuffer commandBuffer) {
    const auto gc = glm::uvec2{tileSize/32u};
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("spectral_height_fields"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("spectral_height_fields"), 0, 1, &m_descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, m_compute.layout("spectral_height_fields"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_controls), &m_controls);
    vkCmdDispatch(commandBuffer, gc.x, gc.y, tileCount);
    Barrier::computeWriteToRead(commandBuffer);
}

void FFTOcean2::generateTemporalHeightField(VkCommandBuffer commandBuffer) {
    const auto gc = glm::uvec2{tileSize/32u};
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("height_field"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("height_field"), 0, 1, &m_descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gc.x, gc.y, tileCount);
    Barrier::computeWriteToRead(commandBuffer);
}

void FFTOcean2::inverseFFT(VkCommandBuffer commandBuffer) {
    inverseFFT(commandBuffer, m_fftDescriptorSet[0]);
    inverseFFT(commandBuffer, m_fftDescriptorSet[2]);
    inverseFFT(commandBuffer, m_fftDescriptorSet[4]);
    inverseFFT(commandBuffer, m_fftDescriptorSet[6]);
    inverseFFT(commandBuffer, m_fftDescriptorSet[8]);
    Barrier::computeWriteToRead(commandBuffer);

    inverseFFT(commandBuffer, m_fftDescriptorSet[1]);
    inverseFFT(commandBuffer, m_fftDescriptorSet[3]);
    inverseFFT(commandBuffer, m_fftDescriptorSet[5]);
    inverseFFT(commandBuffer, m_fftDescriptorSet[7]);
    inverseFFT(commandBuffer, m_fftDescriptorSet[9]);
    Barrier::computeWriteToRead(commandBuffer);
}

void FFTOcean2::inverseFFT(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("ifft"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("ifft"), 0, 1, &descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, tileSize, 1, tileCount);
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

void FFTOcean2::visualize(VkCommandBuffer commandBuffer){
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = m_descriptorSet;
    sets[1] = m_sets[1];
    sets[2] = m_uniformsDescriptorSet;

    const auto gc = glm::uvec2{tileSize/32u};
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("visualizer"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("visualizer"), 0, COUNT(sets), sets.data(), 0,nullptr);
    vkCmdPushConstants(commandBuffer, m_compute.layout("visualizer"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_visualizer.constants), &m_visualizer.constants);
    vkCmdDispatch(commandBuffer, gc.x, gc.y, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void FFTOcean2::computeMinMaxHeight(VkCommandBuffer commandBuffer){
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = m_descriptorSet;
    sets[1] = m_sets[1];
    sets[2] = m_uniformsDescriptorSet;

    int pass = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("min_max"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("min_max"), 0, COUNT(sets), sets.data(), 0,nullptr);
    vkCmdPushConstants(commandBuffer, m_compute.layout("min_max"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &pass);
    vkCmdDispatch(commandBuffer, 1024, 1, tileCount);
    Barrier::computeWriteToRead(commandBuffer);

    pass = 1;
    vkCmdPushConstants(commandBuffer, m_compute.layout("min_max"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &pass);
    vkCmdDispatch(commandBuffer, 1, 1, tileCount);
    Barrier::computeWriteToRead(commandBuffer);
}

void FFTOcean2::createPipelines() {
    SubdivisionGrid::createPipelines();

    auto origin = glm::vec2{10};
    auto extent = m_topViewResolution.zw();
    auto bindlessDescriptorSetLayout = const_cast<VulkanDescriptorSetLayout&>(*m_bindlessDescriptor->descriptorSetLayout);

    m_preview.pipeline =
        m_prototypes->cloneScreenSpaceGraphicsPipeline()
            .shaderStage()
                .vertexShader(FileManager::resource("quad.vert.spv"))
                .fragmentShader(FileManager::resource("ocean_preview.frag.spv"))
            .viewportState().clear()
                .viewport()
                    .origin(origin.x, origin.y)
                    .dimension(extent.x, extent.y)
                    .minDepth(0)
                    .maxDepth(1)
                .scissor()
                    .offset(origin.x, origin.y)
                    .extent(extent.x, extent.y)
                .add()
              .layout()
                .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint))
                .addDescriptorSetLayout(bindlessDescriptorSetLayout)
            .name("ocean_preview")
        .build(m_preview.layout);


    m_render.pipeline =
        m_prototypes->cloneGraphicsPipeline()
            .shaderStage()
                .vertexShader(FileManager::resource("ocean_render.vert.spv"))
                .geometryShader(FileManager::resource("ocean_render.geom.spv"))
                .fragmentShader(FileManager::resource("ocean_render.frag.spv"))
            .vertexInputState().clear()
                .addVertexBindingDescription(0, sizeof(glm::vec2), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32_SFLOAT, 0)
            .inputAssemblyState()
                .triangles()
            .rasterizationState()
                .cullNone()
                .polygonModeFill()
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .depthAttachment(VK_FORMAT_D16_UNORM)
            .colorBlendState()
                .attachments(2)
            .layout().clear()
                .addDescriptorSetLayout(*m_layouts[0])
                .addDescriptorSetLayout(*m_layouts[1])
                .addDescriptorSetLayout(m_uniformsDescriptorSetLayout)
                .addDescriptorSetLayout(AppContext::atmosphere().descriptor.uboDescriptorSetLayout)
                .addDescriptorSetLayout(AppContext::atmosphere().descriptor.lutDescriptorSetLayout)
                .name("ocean_render")
        .build(m_render.layout);
}

void FFTOcean2::render(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 5> sets;
    sets[0] = m_sets[0];
    sets[1] = m_sets[1];
    sets[2] = m_uniformsDescriptorSet;
    sets[3] = AppContext::atmosphere().descriptor.uboDescriptorSet;
    sets[4] = AppContext::atmosphere().descriptor.lutDescriptorSet;

    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.layout.handle, 0, COUNT(sets), sets.data(), 0,nullptr);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertices.buffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, m_indexes, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexedIndirect(commandBuffer, m_drawBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
}

void FFTOcean2::preview(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_preview.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_preview.layout.handle, 0, 1, &m_bindlessDescriptor->descriptorSet, 0,nullptr);
    vkCmdPushConstants(commandBuffer, m_preview.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint), &m_previewIndex);
    AppContext::renderClipSpaceQuad(commandBuffer);
}


void FFTOcean2::preProcess(VkCommandBuffer commandBuffer) {
    generateSpectralComponents(commandBuffer);
    generateSpectralHeightField(commandBuffer);
    inverseFFT(commandBuffer);
    generateTemporalHeightField(commandBuffer);
    computeMinMaxHeight(commandBuffer);
    visualize(commandBuffer);
    update(commandBuffer);
    downloadHeightMap(commandBuffer);
}

void FFTOcean2::newFrame() {
    auto view = m_camera->camera.view;
    auto projection = m_camera->camera.proj;

    glm::mat4 model = m_uniforms.cpu->modelMatrix;
    m_uniforms.cpu->modelViewMatrix = view * model;
    m_uniforms.cpu->viewMatrix = view;
    m_uniforms.cpu->cameraMatrix = glm::inverse(view);
    m_uniforms.cpu->viewProjectionMatrix = projection * view;
    m_uniforms.cpu->modelViewProjectionMatrix = projection * view * model;
    m_uniforms.cpu->lodFactor = computeLodFactor();
    m_uniforms.cpu->dmapFactor = m_options.dmapScale;
    m_uniforms.cpu->minLodVariance = std::sqrt(m_options.minLodStdev / 64.f / m_options.dmapScale);
    m_uniforms.cpu->horizontalLength = m_controls.horizontalLength;
    m_uniforms.cpu->tile = to<uint>(m_options.tile);
    m_uniforms.cpu->choppiness = m_options.choppiness;
    m_uniforms.cpu->dimensions = m_screenResolution;
    m_uniforms.cpu->lightDirection = AppContext::sunDirection();
    m_uniforms.cpu->camera = glm::vec4(m_camera->position(), 0);
    m_uniforms.cpu->rho = m_options.rho;
    m_uniforms.cpu->sigma = m_options.sigma;
    m_uniforms.cpu->near = m_camera->near();
    m_uniforms.cpu->far = m_camera->far();
    m_uniforms.cpu->normalFallOff = m_options.normalFallOff;
    m_uniforms.cpu->scatterColor = m_options.scatterColor;

    auto& h = m_uniforms.cpu->heightMinMax;
    m_uniforms.cpu->scatterConstants.x = 1.0f/(h[0].y + h[1].y + h[2].y + h[3].y);

    glm::mat4 rot = glm::rotate(glm::mat4{1}, glm::radians(m_options.lightAzimuth), {0, 1, 0});
    rot = glm::rotate(rot, glm::radians(m_options.lightZenith), {0, 0, 1});
    auto lightDirection = rot * glm::vec4{1, 0, 0, 1};

    AppContext::updateSunDirection(lightDirection.xyz());
    m_uniforms.cpu->lightDirection = lightDirection;

    m_uniforms.cpu->flags = 0;
    m_uniforms.cpu->flags |= uint(m_options.debug != 0 ? 1 << m_options.debug : 0);
    m_uniforms.cpu->flags |= (uint(m_options.wire) << 5);
    m_uniforms.cpu->flags |= (uint(m_options.showTiles) << 6);

    static Frustum frustum;
    Frustum::extractFrustum(frustum, projection * view);
    std::memcpy(m_uniforms.cpu->frustumPlanes.data(), frustum.cp.data(), BYTE_SIZE(frustum.cp));
}

void FFTOcean2::endFrame() {
    m_time += m_timePeriod * timeScale;
    m_controls.time = m_time;
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

    const auto& atmosphere = *AppContext::atmosphere().info.cpu;
    defaultValues.sunSize = atmosphere.sunSize;
    defaultValues.earthCenter = atmosphere.earthCenter;

    for(auto& minMax : defaultValues.heightMinMax) {
        minMax = glm::vec2{ MAX_FLOAT, MIN_FLOAT };
    }

    m_uniforms.gpu = m_device->createCpuVisibleBuffer(&defaultValues, sizeof(UniformData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    m_uniforms.cpu = reinterpret_cast<UniformData*>(m_uniforms.gpu.map());
    m_device->setName<VK_OBJECT_TYPE_BUFFER>("ocean_uniforms", m_uniforms.gpu.buffer);
}

void FFTOcean2::createDescriptorSetLayout() {
    SubdivisionGrid::createDescriptorSetLayout();
	
    m_descriptorSetLayout = 
        m_device->descriptorSetLayoutBuilder()
            .name("ocean_sim_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(tileCount)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(tileCount)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(tileCount)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(4)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(tileCount)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(5)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(tileCount)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(6)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(tileCount)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(7)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(tileCount)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(8)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(tileCount)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(9)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(tileCount)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(10)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(tileCount)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(11)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(tileCount)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();

    m_fftDescriptorSetLayout =
        m_device->descriptorSetLayoutBuilder()
            .name("ocean_fft_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(tileCount)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(tileCount)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

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
            {m_descriptorSetLayout, m_fftDescriptorSetLayout, m_fftDescriptorSetLayout, m_fftDescriptorSetLayout,
             m_fftDescriptorSetLayout, m_fftDescriptorSetLayout, m_fftDescriptorSetLayout, m_fftDescriptorSetLayout,
             m_fftDescriptorSetLayout, m_fftDescriptorSetLayout, m_fftDescriptorSetLayout,
             m_uniformsDescriptorSetLayout} );
    m_descriptorSet = sets[0];

    m_fftDescriptorSet[0] = sets[1];
    m_fftDescriptorSet[1] = sets[2];

    m_fftDescriptorSet[2] = sets[3];
    m_fftDescriptorSet[3] = sets[4];

    m_fftDescriptorSet[4] = sets[5];
    m_fftDescriptorSet[5] = sets[6];

    m_fftDescriptorSet[6] = sets[7];
    m_fftDescriptorSet[7] = sets[8];

    m_fftDescriptorSet[8] = sets[9];
    m_fftDescriptorSet[9] = sets[10];
    
    m_uniformsDescriptorSet = sets[11];

    auto writes = initializers::writeDescriptorSets<33>();

    writes[0].dstSet = m_descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo noiseInfo{ nullptr, m_textures.noise.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[0].pImageInfo = &noiseInfo;

    std::array<std::vector<VkDescriptorImageInfo>, 5> staging{};
    for(auto i = 0; i < 5; ++i) {
        staging[i] = map_range(m_views.staging[i], [](auto view) { return VkDescriptorImageInfo{nullptr, view.handle, VK_IMAGE_LAYOUT_GENERAL };  });
    }

    writes[1].dstSet = m_descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = COUNT(staging[0]);
    writes[1].pImageInfo = staging[0].data();

    writes[2].dstSet = m_descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = COUNT(staging[1]);
    writes[2].pImageInfo = staging[1].data();

    auto spectralHeightFieldInfos = map_range(m_views.fftHeightField, [](auto view) { return VkDescriptorImageInfo{nullptr, view.handle, VK_IMAGE_LAYOUT_GENERAL };  });
    writes[3].dstSet = m_descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].descriptorCount = COUNT(spectralHeightFieldInfos);
    writes[3].pImageInfo = spectralHeightFieldInfos.data();

    auto heightFieldInfos = map_range(m_views.heightField, [](auto view) { return VkDescriptorImageInfo{nullptr, view.handle, VK_IMAGE_LAYOUT_GENERAL };  });
    writes[4].dstSet = m_descriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[4].descriptorCount = COUNT(heightFieldInfos);
    writes[4].pImageInfo = heightFieldInfos.data();

    auto spectralHeightFieldXInfos = map_range(m_views.fftHeightFieldX, [](auto view) { return VkDescriptorImageInfo{nullptr, view.handle, VK_IMAGE_LAYOUT_GENERAL };  });
    writes[5].dstSet = m_descriptorSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[5].descriptorCount = COUNT(spectralHeightFieldXInfos);
    writes[5].pImageInfo = spectralHeightFieldXInfos.data();

    auto spectralHeightFieldZInfos = map_range(m_views.fftHeightFieldZ, [](auto view) { return VkDescriptorImageInfo{nullptr, view.handle, VK_IMAGE_LAYOUT_GENERAL };  });
    writes[6].dstSet = m_descriptorSet;
    writes[6].dstBinding = 6;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[6].descriptorCount = COUNT(spectralHeightFieldZInfos);
    writes[6].pImageInfo = spectralHeightFieldZInfos.data();

    auto spectralSlopeXInfos = map_range(m_views.fftSlopeX, [](auto view) { return VkDescriptorImageInfo{nullptr, view.handle, VK_IMAGE_LAYOUT_GENERAL };  });
    writes[7].dstSet = m_descriptorSet;
    writes[7].dstBinding = 7;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[7].descriptorCount = COUNT(spectralSlopeXInfos);
    writes[7].pImageInfo = spectralSlopeXInfos.data();

    auto spectralSlopeZInfos = map_range(m_views.fftSlopeZ, [](auto view) { return VkDescriptorImageInfo{nullptr, view.handle, VK_IMAGE_LAYOUT_GENERAL };  });
    writes[8].dstSet = m_descriptorSet;
    writes[8].dstBinding = 8;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[8].descriptorCount = COUNT(spectralSlopeZInfos);
    writes[8].pImageInfo = spectralSlopeZInfos.data();

    // inverse fft height field
    writes[9].dstSet = m_fftDescriptorSet[0];
    writes[9].dstBinding = 0;
    writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[9].descriptorCount = COUNT(spectralHeightFieldInfos);
    writes[9].pImageInfo = spectralHeightFieldInfos.data();

    writes[10].dstSet = m_fftDescriptorSet[0];
    writes[10].dstBinding = 1;
    writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[10].descriptorCount = COUNT(staging[0]);
    writes[10].pImageInfo = staging[0].data();

    writes[11].dstSet = m_fftDescriptorSet[1];
    writes[11].dstBinding = 0;
    writes[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[11].descriptorCount = COUNT(staging[0]);
    writes[11].pImageInfo = staging[0].data();

    writes[12].dstSet = m_fftDescriptorSet[1];
    writes[12].dstBinding = 1;
    writes[12].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[12].descriptorCount = COUNT(spectralHeightFieldInfos);
    writes[12].pImageInfo = spectralHeightFieldInfos.data();

    // inverse fft height field X
    writes[13].dstSet = m_fftDescriptorSet[2];
    writes[13].dstBinding = 0;
    writes[13].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[13].descriptorCount = COUNT(spectralHeightFieldXInfos);
    writes[13].pImageInfo = spectralHeightFieldXInfos.data();

    writes[14].dstSet = m_fftDescriptorSet[2];
    writes[14].dstBinding = 1;
    writes[14].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[14].descriptorCount = COUNT(staging[1]);
    writes[14].pImageInfo = staging[1].data();

    writes[15].dstSet = m_fftDescriptorSet[3];
    writes[15].dstBinding = 0;
    writes[15].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[15].descriptorCount = COUNT(staging[1]);
    writes[15].pImageInfo = staging[1].data();

    writes[16].dstSet = m_fftDescriptorSet[3];
    writes[16].dstBinding = 1;
    writes[16].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[16].descriptorCount = COUNT(spectralHeightFieldXInfos);
    writes[16].pImageInfo = spectralHeightFieldXInfos.data();

    // inverse fft height field Z
    writes[17].dstSet = m_fftDescriptorSet[4];
    writes[17].dstBinding = 0;
    writes[17].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[17].descriptorCount = COUNT(spectralHeightFieldZInfos);
    writes[17].pImageInfo = spectralHeightFieldZInfos.data();

    writes[18].dstSet = m_fftDescriptorSet[4];
    writes[18].dstBinding = 1;
    writes[18].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[18].descriptorCount = COUNT(staging[2]);
    writes[18].pImageInfo = staging[2].data();

    writes[19].dstSet = m_fftDescriptorSet[5];
    writes[19].dstBinding = 0;
    writes[19].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[19].descriptorCount = COUNT(staging[2]);
    writes[19].pImageInfo = staging[2].data();

    writes[20].dstSet = m_fftDescriptorSet[5];
    writes[20].dstBinding = 1;
    writes[20].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[20].descriptorCount = COUNT(spectralHeightFieldZInfos);
    writes[20].pImageInfo = spectralHeightFieldZInfos.data();


    // inverse fft slope field X
    writes[21].dstSet = m_fftDescriptorSet[6];
    writes[21].dstBinding = 0;
    writes[21].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[21].descriptorCount = COUNT(spectralSlopeXInfos);
    writes[21].pImageInfo = spectralSlopeXInfos.data();

    writes[22].dstSet = m_fftDescriptorSet[6];
    writes[22].dstBinding = 1;
    writes[22].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[22].descriptorCount = COUNT(staging[3]);
    writes[22].pImageInfo = staging[3].data();

    writes[23].dstSet = m_fftDescriptorSet[7];
    writes[23].dstBinding = 0;
    writes[23].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[23].descriptorCount = COUNT(staging[3]);
    writes[23].pImageInfo = staging[3].data();

    writes[24].dstSet = m_fftDescriptorSet[7];
    writes[24].dstBinding = 1;
    writes[24].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[24].descriptorCount = COUNT(spectralSlopeXInfos);
    writes[24].pImageInfo = spectralSlopeXInfos.data();


    // inverse fft slope field Z
    writes[25].dstSet = m_fftDescriptorSet[8];
    writes[25].dstBinding = 0;
    writes[25].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[25].descriptorCount = COUNT(spectralSlopeZInfos);
    writes[25].pImageInfo = spectralSlopeZInfos.data();

    writes[26].dstSet = m_fftDescriptorSet[8];
    writes[26].dstBinding = 1;
    writes[26].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[26].descriptorCount = COUNT(staging[4]);
    writes[26].pImageInfo = staging[4].data();

    writes[27].dstSet = m_fftDescriptorSet[9];
    writes[27].dstBinding = 0;
    writes[27].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[27].descriptorCount = COUNT(staging[4]);
    writes[27].pImageInfo = staging[4].data();

    writes[28].dstSet = m_fftDescriptorSet[9];
    writes[28].dstBinding = 1;
    writes[28].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[28].descriptorCount = COUNT(spectralSlopeZInfos);
    writes[28].pImageInfo = spectralSlopeZInfos.data();

    auto normalInfo = map_range(m_views.normalMap, [](auto view){ return VkDescriptorImageInfo{nullptr, view.handle, VK_IMAGE_LAYOUT_GENERAL };  });
    writes[29].dstSet = m_descriptorSet;
    writes[29].dstBinding = 9;
    writes[29].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[29].descriptorCount = COUNT(normalInfo);
    writes[29].pImageInfo = normalInfo.data();
    
    writes[30].dstSet = m_uniformsDescriptorSet;
    writes[30].dstBinding = 0;
    writes[30].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[30].descriptorCount = 1;
    VkDescriptorBufferInfo uniformsInfo{ m_uniforms.gpu, 0, VK_WHOLE_SIZE };
    writes[30].pBufferInfo = &uniformsInfo;

    writes[31].dstSet = m_descriptorSet;
    writes[31].dstBinding = 10;
    writes[31].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[31].descriptorCount = 1;
    VkDescriptorImageInfo visualizeInfo{ nullptr, m_visualizer.texture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[31].pImageInfo = &visualizeInfo;

    writes[32].dstSet = m_descriptorSet;
    writes[32].dstBinding = 11;
    writes[32].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[32].descriptorCount = 1;
    VkDescriptorImageInfo minMaxInfo{ nullptr, m_textures.minMax.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[32].pImageInfo = &minMaxInfo;

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

void FFTOcean2::createSimTextures() {
    m_textures.staging[0].layers = tileCount;
    m_textures.staging[1].layers = tileCount;
    m_textures.staging[2].layers = tileCount;
    m_textures.staging[3].layers = tileCount;
    m_textures.staging[4].layers = tileCount;
    m_textures.fftHeightField.layers = tileCount;
    m_textures.heightField.layers = tileCount;
    m_textures.fftHeightFieldX.layers = tileCount;
    m_textures.fftHeightFieldZ.layers = tileCount;
    m_textures.fftSlopeX.layers = tileCount;
    m_textures.fftSlopeZ.layers = tileCount;
    m_textures.normalMap.layers = tileCount;
    m_textures.minMax.layers = tileCount;
    m_textures.minMax.levels = to<uint>(std::log2(tileSize)) + 1;

    textures::createNoTransition(*m_device, m_textures.noise, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.staging[0], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.staging[1], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.staging[2], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.staging[3], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.staging[4], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.fftHeightField, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.fftHeightFieldX, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.fftHeightFieldZ, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.fftSlopeX, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.fftSlopeZ, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.normalMap, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.heightField, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.minMax, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_visualizer.texture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {tileSize, tileSize, 1});


    auto subResource = DEFAULT_SUB_RANGE;
    subResource.layerCount = tileCount;
    m_device->graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        Barriers::push(m_textures.noise.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(m_textures.staging[0].image, subResource, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(m_textures.staging[1].image, subResource, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(m_textures.staging[2].image, subResource, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(m_textures.staging[3].image, subResource, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(m_textures.staging[4].image, subResource, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(m_textures.fftHeightField.image, subResource, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(m_textures.heightField.image, subResource, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(m_textures.fftHeightFieldX.image, subResource, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(m_textures.fftHeightFieldZ.image, subResource, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(m_textures.fftSlopeX.image, subResource, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(m_textures.fftSlopeZ.image, subResource, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(m_textures.normalMap.image, subResource, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(m_visualizer.texture.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::flush(commandBuffer);
    });

    subResource.layerCount = 1;
    for(auto i = 0; i < tileCount; ++i) {
        subResource.baseArrayLayer = i;
        m_views.staging[0][i] = m_textures.staging[0].image.createView(VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.staging[1][i] = m_textures.staging[1].image.createView(VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.staging[2][i] = m_textures.staging[2].image.createView(VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.staging[3][i] = m_textures.staging[3].image.createView(VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.staging[4][i] = m_textures.staging[4].image.createView(VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.fftHeightField[i] =  m_textures.fftHeightField.image.createView(VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.fftHeightFieldX[i] =  m_textures.fftHeightFieldX.image.createView(VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.fftHeightFieldZ[i] =  m_textures.fftHeightFieldZ.image.createView(VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.fftSlopeX[i] =  m_textures.fftSlopeX.image.createView(VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.fftSlopeZ[i] =  m_textures.fftSlopeZ.image.createView(VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.heightField[i] =  m_textures.heightField.image.createView(VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.normalMap[i] =  m_textures.normalMap.image.createView(VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
    }


    m_heightMapIndex = m_bindlessDescriptor->reserveTextureSlots(2);
    m_normalIndex = m_heightMapIndex + 1;
    m_previewIndex = m_heightMapIndex;
    m_bindlessDescriptor->update({ &m_textures.heightField, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_heightMapIndex, VK_IMAGE_LAYOUT_GENERAL });
    m_bindlessDescriptor->update({ &m_textures.heightField, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_previewIndex, VK_IMAGE_LAYOUT_GENERAL });
    m_bindlessDescriptor->update({ &m_textures.normalMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_normalIndex, VK_IMAGE_LAYOUT_GENERAL });

    size_t tileSizeBytes = sizeof(glm::vec4) * tileSize * tileSize;
    m_heightMapBuffer = m_device->createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, tileSizeBytes * tileCount, "height_map");
    auto mapping = reinterpret_cast<char*>(m_heightMapBuffer.map());
    m_heightMap[0] = { reinterpret_cast<glm::vec4*>(mapping), tileSizeBytes };
    m_heightMap[1] = { reinterpret_cast<glm::vec4*>(mapping + tileSizeBytes), tileSizeBytes };
    m_heightMap[2] = { reinterpret_cast<glm::vec4*>(mapping + tileSizeBytes * 2), tileSizeBytes };
    m_heightMap[3] = { reinterpret_cast<glm::vec4*>(mapping + tileSizeBytes * 2), tileSizeBytes };

}

std::vector<PipelineMetaData> FFTOcean2::additionalMetadata() {
    auto bindlessDescriptorSetLayout = const_cast<VulkanDescriptorSetLayout*>(m_bindlessDescriptor->descriptorSetLayout);
    return {
            {
                    .name = "generate_gaussian_distribution",
                    .shadePath = FileManager::resource("gaussian_noise_distribution.comp.spv"),
                    .layouts = { &m_descriptorSetLayout },
            },
            {
                    .name = "spectral_components",
                    .shadePath = FileManager::resource("ocean_spectral_components.comp.spv"),
                    .layouts = { &m_descriptorSetLayout },
                    .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_controls)} },
            },
            {
                    .name = "spectral_height_fields",
                    .shadePath = FileManager::resource("ocean_spectral_height_fields.comp.spv"),
                    .layouts = { &m_descriptorSetLayout },
                    .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_controls)} },
            },
            {
                    .name = "height_field",
                    .shadePath = FileManager::resource("ocean_height_field.comp.spv"),
                    .layouts = { &m_descriptorSetLayout },
            },
            {
                    .name = "ifft",
                    .shadePath = FileManager::resource("ocean_ifft.comp.spv"),
                    .layouts = { &m_fftDescriptorSetLayout },
                    .specializationConstants = {
                            .entries = { {0, 0, sizeof(uint)} },
                            .data = const_cast<uint*>(&tileSize),
                            .dataSize = sizeof(uint)
                    }
            },
            {
                    .name = "visualizer",
                    .shadePath = FileManager::resource("ocean_visualizer.comp.spv"),
                    .layouts = { &m_descriptorSetLayout, bindlessDescriptorSetLayout, &m_uniformsDescriptorSetLayout  },
                    .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_visualizer.constants)} }
            },
            {
                    .name = "min_max",
                    .shadePath = FileManager::resource("ocean_min_max.comp.spv"),
                    .layouts = { &m_descriptorSetLayout, bindlessDescriptorSetLayout, &m_uniformsDescriptorSetLayout  },
                    .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int)} },
                    .specializationConstants = {
                            .entries = { {0, 0, sizeof(uint)} },
                            .data = const_cast<uint*>(&tileSize),
                            .dataSize = sizeof(uint)
                    }
            }
    };
}

void FFTOcean2::controls(bool show, bool composite) {
    if(!show) return;

    if(!composite){
        ImGui::Begin("ocean");
        ImGui::SetWindowSize({});
    }

    ImGui::SliderFloat("Pixels/Edge", &m_options.primitivePixelLengthTarget, 1, 32);
    ImGui::SliderFloat("Dmap scale", &m_options.dmapScale, 0, 1);
    ImGui::SliderFloat("Lod Std", &m_options.minLodStdev, 0, 1);
    ImGui::SliderFloat("Choppiness", &m_options.choppiness, 0, 5);
    ImGui::SliderInt("tiles", &m_options.tile, 1, tileCount);

    ImGui::Checkbox("Wire", &m_options.wire);
    ImGui::SameLine();
    ImGui::Checkbox("topView", &m_options.topView);
    ImGui::Checkbox("Show tiles", &m_options.showTiles);
    ImGui::Checkbox("Show visualizer", &m_options.visualizer);

    if(ImGui::CollapsingHeader("lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Zenith Angle", &m_options.lightZenith, -30, 180);
        ImGui::SliderFloat("Azimuth Angle", &m_options.lightAzimuth, 0, 360);
        ImGui::SliderFloat("Normal Att.", &m_options.normalFallOff, 1, 100);
        ImGui::ColorEdit3("scatter Col.", &m_options.scatterColor.x);
    }


    static std::array<const char*, 5> labels{"None","Normal","Scatter","Specular","Reflection"};
    ImGui::Combo("debug", &m_options.debug, labels.data(), labels.size());

    const auto& cp = m_camera->position();
    const auto& cd = m_camera->viewDir;
    ImGui::Text("Wave Info\n\tmax height: %.3f", m_uniforms.cpu->heightMinMax[0].y);
    ImGui::Text("Camera:\n\tposition: %.3f, %.3f, %.3f,\n\tdirection: %.3f, %.3f, %.3f", cp.x, cp.y, cp.z, cd.x, cd.y, cd.z);
    ImGui::Text("Cbt info:\n\tNode Count: %d\n\tMax depth: %d", m_cbtInfo.cpu->nodeCount, m_cbtInfo.cpu->maxDepth);

    if(!composite) ImGui::End();
}

void FFTOcean2::renderTopView(VkCommandBuffer commandBuffer) {
    if(!m_options.topView) return;
    topView(commandBuffer);
}

void FFTOcean2::visualizer(ImGuiPlugin& plugin) {
    if(!m_options.visualizer) return;
    static ImTextureID texId = plugin.addTexture(m_visualizer.texture, VK_IMAGE_LAYOUT_GENERAL);
    static std::array<const char*, 7> viewLabels{"Patches", "Normals", "Jacobian normals", "Jacobian", "Jacobian eigan values", "Jacobian eigan vectors", "Surface profile"};

    static int view = 0;
    static float scale = 0.1;
    static bool combined = false;

    ImGui::Begin("visualizer");
    ImGui::SetWindowSize({0, 0});

    ImGui::Image(texId, {300, 300});
    ImGui::Combo("View", &view, viewLabels.data(), COUNT(viewLabels));

    if(view == 6) {
        ImGui::SliderFloat("Scale", &scale, 0.01, 1);
    }

    ImGui::End();

    m_visualizer.constants.view = view;
    m_visualizer.constants.scale = scale;
    m_visualizer.constants.flag = int(combined);

}

void FFTOcean2::updateMouse(glm::ivec2 mouse, int state) {
    m_uniforms.cpu->mouse = glm::vec4(mouse, state, 0);
}

void FFTOcean2::refresh(Prototypes& prototypes) {
    m_prototypes = &prototypes;
    createPipelines();
}

uint FFTOcean2::heightMapTextureIndex() const {
    return m_heightMapIndex;
}

glm::vec4 FFTOcean2::patchLengths() const {
    return m_uniforms.cpu->horizontalLength;
}

void FFTOcean2::downloadHeightMap(VkCommandBuffer commandBuffer) {
    Barriers::pushAndFlush(commandBuffer, m_textures.heightField.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    static bool once = true;
    static VkBufferImageCopy2 region{
        .sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = tileCount,
        },
        .imageExtent = {tileSize, tileSize, 1},
    };


    static VkCopyImageToBufferInfo2 copyInfo{
        .sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2,
        .srcImage = m_textures.heightField.image,
        .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .dstBuffer = m_heightMapBuffer,
        .regionCount = 1,
        .pRegions = &region
    };

    vkCmdCopyImageToBuffer2(commandBuffer, &copyInfo);

    Barriers::pushAndFlush(commandBuffer, m_textures.heightField.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

}

float FFTOcean2::sampleHeight(glm::vec2 position) {
    float H = 0;

    auto patches = m_uniforms.cpu->horizontalLength;
    for(auto i = 0; i < tileCount; ++i) {
        auto uv = glm::uvec2{ glm::fract(position/patches[i])} * tileSize;
        auto index = uv.y * tileSize + uv.y;
        H += m_heightMap[i][index].y;
    }
    return H;
}

