#include "FFTOcean2.hpp"
#include "Barrier.hpp"
#include "filemanager.hpp"
#include "Vertex.h"
#include "GraphicsPipelineBuilder.hpp"
#include "AppContext.hpp"

FFTOcean2::FFTOcean2(VulkanDevice &device, VulkanDescriptorPool &descriptorPool, BindlessDescriptor &bindlessDescriptor,
                     Profiler &profiler, glm::vec2 resolution)
        : SubdivisionGrid(device, descriptorPool, bindlessDescriptor, "fft_ocean",
                          {10, resolution.y - 512, 512, 512}, 1, &profiler) {

}

void FFTOcean2::init() {
    createSimTextures();
    SubdivisionGrid::init();
    m_device->graphicsCommandPool().oneTimeCommand([this](auto commandBuffer){
        generateGaussianNoise(commandBuffer);
    });
}

void FFTOcean2::preProcess(VkCommandBuffer commandBuffer) {
    generateSpectralComponents(commandBuffer);
    generateSpectralHeightField(commandBuffer);
    inverseFFT(commandBuffer);
    generateTemporalHeightField(commandBuffer);
}

void FFTOcean2::newFrame() {

}

void FFTOcean2::endFrame() {
    m_time += m_timePeriod;
    m_controls.time = m_time;
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

    textures::createNoTransition(*m_device, m_textures.noise, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.staging[0], VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.staging[1], VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.staging[2], VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.staging[3], VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.staging[4], VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.fftHeightField, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.fftHeightFieldX, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.fftHeightFieldZ, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.fftSlopeX, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.fftSlopeZ, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.normalMap, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {tileSize, tileSize, 1});
    textures::createNoTransition(*m_device, m_textures.heightField, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {tileSize, tileSize, 1});


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
        Barriers::flush(commandBuffer);
    });

    subResource.layerCount = 1;
    for(auto i = 0; i < tileCount; ++i) {
        subResource.baseArrayLayer = i;
        m_views.staging[0][i] = m_textures.staging[0].image.createView(VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.staging[1][i] = m_textures.staging[1].image.createView(VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.staging[2][i] = m_textures.staging[2].image.createView(VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.staging[3][i] = m_textures.staging[3].image.createView(VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.staging[4][i] = m_textures.staging[4].image.createView(VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.fftHeightField[i] =  m_textures.fftHeightField.image.createView(VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.fftHeightFieldX[i] =  m_textures.fftHeightFieldX.image.createView(VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.fftHeightFieldZ[i] =  m_textures.fftHeightFieldZ.image.createView(VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.fftSlopeX[i] =  m_textures.fftSlopeX.image.createView(VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.fftSlopeZ[i] =  m_textures.fftSlopeZ.image.createView(VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.heightField[i] =  m_textures.heightField.image.createView(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
        m_views.normalMap[i] =  m_textures.normalMap.image.createView(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subResource);
    }


    m_previewIndex = m_bindlessDescriptor->reserveTextureSlots(1);
    m_bindlessDescriptor->update({ &m_textures.normalMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_previewIndex, VK_IMAGE_LAYOUT_GENERAL });
}

PipelineMetaData FFTOcean2::subdivisionMetadata() {
    return {
        .name = "ocean_subdivision",
        .shadePath = FileManager::resource("ocean_subdivision.comp.spv"),
    };
}

std::vector<PipelineMetaData> FFTOcean2::additionalMetadata() {

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
    };
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

}

void FFTOcean2::createPipelines() {
    SubdivisionGrid::createPipelines();
	
	const auto origin = glm::vec2{10};
    const auto extent = m_topViewResolution.zw();

    auto bindlessDescriptorSetLayout = const_cast<VulkanDescriptorSetLayout&>(*m_bindlessDescriptor->descriptorSetLayout);
    m_preview.pipeline =
        m_device->graphicsPipelineBuilder()
            .shaderStage()
                .vertexShader(FileManager::resource("quad.vert.spv"))
                .fragmentShader(FileManager::resource("ocean_preview.frag.spv"))
            .vertexInputState()
                .addVertexBindingDescriptions(ClipSpace::bindingDescription())
                .addVertexAttributeDescriptions(ClipSpace::attributeDescriptions())
            .inputAssemblyState()
                .triangleStrip()
            .viewportState()
                .viewport()
                    .origin(origin.x, origin.y)
                    .dimension(extent.x, extent.y)
                    .minDepth(0)
                    .maxDepth(1)
                .scissor()
                    .offset(origin.x, origin.y)
                    .extent(extent.x, extent.y)
                .add()
                .rasterizationState()
                    .cullNone()
                    .frontFaceCounterClockwise()
                    .polygonModeFill()
                .multisampleState()
                    .rasterizationSamples(m_device->settings.msaaSamples)
                .depthStencilState()
                    .enableDepthWrite()
                    .enableDepthTest()
                    .compareOpAlways()
                    .minDepthBounds(0)
                    .maxDepthBounds(1)
                .colorBlendState()
                    .attachment()
                    .add()
                .layout()
                    .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint))
                    .addDescriptorSetLayout(bindlessDescriptorSetLayout)
                .renderPass(AppContext::renderPass())
                .subpass(0)
                .name("ocean_preview")
        .build(m_preview.layout);
}

void FFTOcean2::preview(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_preview.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_preview.layout.handle, 0, 1, &m_bindlessDescriptor->descriptorSet, 0,nullptr);
    vkCmdPushConstants(commandBuffer, m_preview.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint), &m_previewIndex);
    AppContext::renderClipSpaceQuad(commandBuffer);
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
}

void FFTOcean2::updateDescriptorSets() {
    SubdivisionGrid::updateDescriptorSets();
    
    auto sets = m_descriptorPool->allocate(
            {m_descriptorSetLayout, m_fftDescriptorSetLayout, m_fftDescriptorSetLayout, m_fftDescriptorSetLayout,
             m_fftDescriptorSetLayout, m_fftDescriptorSetLayout, m_fftDescriptorSetLayout, m_fftDescriptorSetLayout,
             m_fftDescriptorSetLayout, m_fftDescriptorSetLayout, m_fftDescriptorSetLayout,
             } );
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

    auto writes = initializers::writeDescriptorSets<30>();

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

    m_device->updateDescriptorSets(writes);
}