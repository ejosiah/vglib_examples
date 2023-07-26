#include "atmosphere.hpp"
#include "glm_format.h"
#include <spdlog/spdlog.h>
#include "Texture.h"
#include <array>
#include <imgui.h>

Atmosphere::Atmosphere(VulkanDevice* device, VulkanDescriptorPool* descriptorPool, FileManager* fileManager)
: m_device{device},
  m_descriptorPool{ descriptorPool },
  m_fileMgr{ fileManager }
{
   createBuffers();
   createSampler();
   createLutTextures();
   createBarriers();
   refresh();
   createDescriptorSetLayout();
   updateDescriptorSet();
   createPipelines();
   spdlog::info("size of DensityProfileLayer: {}", sizeof(DensityProfileLayer));
}

void Atmosphere::generateLUT() {
    refresh();
    m_device->graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        computeTransmittanceLUT(commandBuffer);

        barrier(commandBuffer, { TRANSMITTANCE_BARRIER });

        computeDirectIrradiance(commandBuffer);
        computeSingleScattering(commandBuffer);

        barrier(commandBuffer, { IRRADIANCE_BARRIER, DELTA_IRRADIANCE_BARRIER, DELTA_RAYLEIGH_BARRIER, DELTA_MIE_BARRIER });

        for(int scatteringOrder = 2; scatteringOrder <= params.numScatteringOrder; ++scatteringOrder){
            computeScatteringDensity(commandBuffer, scatteringOrder);
            computeIndirectIrradiance(commandBuffer, scatteringOrder - 1);

            barrier(commandBuffer, {DELTA_SCATTERING_DENSITY_BARRIER});

            computeMultipleScattering(commandBuffer);

            barrier(commandBuffer, {DELTA_MULTIPLE_DENSITY_BARRIER, SCATTERING_BARRIER});

        }

    });
}

void Atmosphere::barrier(VkCommandBuffer commandBuffer, std::vector<int> images) {
    std::vector<VkImageMemoryBarrier> barriers{};
    for(auto image : images) barriers.push_back(m_barriers[image]);

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            , 0, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, COUNT(barriers), barriers.data());
}

void Atmosphere::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    sampler = m_device->createSampler(samplerInfo);
}

void Atmosphere::createLutTextures() {
     textures::create(*m_device, transmittanceLUT, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT
                                        , {TRANSMITTANCE_TEXTURE_WIDTH, TRANSMITTANCE_TEXTURE_HEIGHT, 1}
                                        , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float ));

     textures::create(*m_device, irradianceLut, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT
                                        , {IRRADIANCE_TEXTURE_WIDTH, IRRADIANCE_TEXTURE_HEIGHT, 1}
                                        , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float ));

     textures::create(*m_device, scatteringLUT, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT
                                        , {SCATTERING_TEXTURE_WIDTH, SCATTERING_TEXTURE_HEIGHT, SCATTERING_TEXTURE_DEPTH}
                                        , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float ));

    textures::create(*m_device, deltaIrradianceTexture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT
            , {IRRADIANCE_TEXTURE_WIDTH, IRRADIANCE_TEXTURE_HEIGHT, 1}
            , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float ));

     textures::create(*m_device, deltaRayleighScatteringTexture, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT
                                        , {SCATTERING_TEXTURE_WIDTH, SCATTERING_TEXTURE_HEIGHT, SCATTERING_TEXTURE_DEPTH}
                                        , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float ));
     textures::create(*m_device, deltaMieScatteringTexture, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT
                                        , {SCATTERING_TEXTURE_WIDTH, SCATTERING_TEXTURE_HEIGHT, SCATTERING_TEXTURE_DEPTH}
                                        , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float ));
     textures::create(*m_device, deltaScatteringDensityTexture, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT
                                        , {SCATTERING_TEXTURE_WIDTH, SCATTERING_TEXTURE_HEIGHT, SCATTERING_TEXTURE_DEPTH}
                                        , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float ));
     textures::create(*m_device, deltaMultipleScatteringTexture, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT
                                        , {SCATTERING_TEXTURE_WIDTH, SCATTERING_TEXTURE_HEIGHT, SCATTERING_TEXTURE_DEPTH}
                                        , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float ));

    transmittanceLUT.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    irradianceLut.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    scatteringLUT.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    deltaIrradianceTexture.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    deltaRayleighScatteringTexture.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    deltaMieScatteringTexture.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    deltaScatteringDensityTexture.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    deltaMultipleScatteringTexture.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
}

void Atmosphere::createDescriptorSetLayout() {
    uboDescriptorSetLayout =
        m_device->descriptorSetLayoutBuilder()
            .name("atmosphere_ubo")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(NUM_DENSITY_PROFILES)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();

    lutDescriptorSetLayout =
        m_device->descriptorSetLayoutBuilder()
            .name("atmosphere_lut")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
                .immutableSamplers(sampler)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
                .immutableSamplers(sampler)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
                .immutableSamplers(sampler)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
                .immutableSamplers(sampler)
        .createLayout();

    imageDescriptorSetLayout =
        m_device->descriptorSetLayoutBuilder()
            .name("atmosphere_compute_lut")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    tempDescriptorSetLayout =
        m_device->descriptorSetLayoutBuilder()
            .name("atmosphere_temp_compute_lut")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(4)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(5)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                .immutableSamplers(sampler)
            .binding(6)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                .immutableSamplers(sampler)
            .binding(7)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                .immutableSamplers(sampler)
            .binding(8)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                .immutableSamplers(sampler)
            .binding(9)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                .immutableSamplers(sampler)
        .createLayout();

    auto sets = m_descriptorPool->allocate({uboDescriptorSetLayout, lutDescriptorSetLayout, imageDescriptorSetLayout, tempDescriptorSetLayout});

    uboDescriptorSet = sets[0];
    lutDescriptorSet = sets[1];
    imageDescriptorSet = sets[2];
    tempDescriptorSet = sets[3];
}

void Atmosphere::updateDescriptorSet() {
    
    auto writes = initializers::writeDescriptorSets<2>();
    
    writes[0].dstSet = uboDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uboInfo{m_uboBuffer, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &uboInfo;

    VkDeviceSize DensityProfileLayerSize = sizeof(DensityProfileLayer);
    std::array<VkDescriptorBufferInfo, NUM_DENSITY_PROFILES> layerInfos{
            VkDescriptorBufferInfo{m_densityProfileBuffer, 0, DensityProfileLayerSize},
            VkDescriptorBufferInfo{m_densityProfileBuffer, DensityProfileLayerSize * 1, DensityProfileLayerSize},
            VkDescriptorBufferInfo{m_densityProfileBuffer, DensityProfileLayerSize * 2, DensityProfileLayerSize},
            VkDescriptorBufferInfo{m_densityProfileBuffer, DensityProfileLayerSize * 3, DensityProfileLayerSize},
    };
    
    writes[1].dstSet = uboDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = layerInfos.size();
    writes[1].pBufferInfo = layerInfos.data();
    m_device->updateDescriptorSets(writes);

    writes = initializers::writeDescriptorSets<4>();

    writes[0].dstSet = lutDescriptorSet ;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo irradianceInfo{ VK_NULL_HANDLE, irradianceLut.imageView, VK_IMAGE_LAYOUT_GENERAL};
    writes[0].pImageInfo = &irradianceInfo;

    writes[1].dstSet = lutDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo transmittanceInfo{ VK_NULL_HANDLE, transmittanceLUT.imageView, VK_IMAGE_LAYOUT_GENERAL};
    writes[1].pImageInfo = &transmittanceInfo;

    writes[2].dstSet = lutDescriptorSet ;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo scatteringInfo{ VK_NULL_HANDLE, scatteringLUT.imageView, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &scatteringInfo;

    // single_mie_scattering
    writes[3] = writes[2];
    writes[3].dstBinding = 3;
    m_device->updateDescriptorSets(writes);

    // images
    writes[0].dstSet = imageDescriptorSet;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    writes[1].dstSet = imageDescriptorSet;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    writes[2].dstSet = imageDescriptorSet;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    writes[3].dstSet = imageDescriptorSet;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    m_device->updateDescriptorSets(writes);

    // temp writes
    writes = initializers::writeDescriptorSets<10>();
    writes[0].dstSet = tempDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo deltaIrradianceInfo{VK_NULL_HANDLE, deltaIrradianceTexture.imageView, VK_IMAGE_LAYOUT_GENERAL};
    writes[0].pImageInfo = &deltaIrradianceInfo;

    writes[1].dstSet = tempDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo deltaRayleighInfo{VK_NULL_HANDLE, deltaRayleighScatteringTexture.imageView, VK_IMAGE_LAYOUT_GENERAL};
    writes[1].pImageInfo = &deltaRayleighInfo;

    writes[2].dstSet = tempDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo deltaMieInfo{VK_NULL_HANDLE, deltaMieScatteringTexture.imageView, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &deltaMieInfo;

    writes[3].dstSet = tempDescriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo deltaScatteringInfo{VK_NULL_HANDLE, deltaScatteringDensityTexture.imageView, VK_IMAGE_LAYOUT_GENERAL};
    writes[3].pImageInfo = &deltaScatteringInfo;

    writes[4].dstSet = tempDescriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[4].descriptorCount = 1;
    VkDescriptorImageInfo deltaMultipleScatteringInfo{VK_NULL_HANDLE, deltaMultipleScatteringTexture.imageView, VK_IMAGE_LAYOUT_GENERAL};
    writes[4].pImageInfo = &deltaMultipleScatteringInfo;

    writes[5] = writes[0];
    writes[5].dstBinding = 5;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    writes[6] = writes[1];
    writes[6].dstBinding = 6;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    writes[7] = writes[2];
    writes[7].dstBinding = 7;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    writes[8] = writes[4];
    writes[8].dstBinding = 8;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    writes[9] = writes[3];
    writes[9].dstBinding = 9;
    writes[9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;


    m_device->updateDescriptorSets(writes);

}

void Atmosphere::createBuffers() {
    m_uboBuffer = m_device->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(UBO), "atmosphere_params");
    m_densityProfileBuffer = m_device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(DensityProfileLayer) * 4, "density_profile_layers");

    ubo = reinterpret_cast<UBO*>(m_uboBuffer.map());
    layers = reinterpret_cast<DensityProfileLayer*>(m_densityProfileBuffer.map());
}

void Atmosphere::createBarriers() {
    m_barriers.resize(NUM_BARRIERS);
    for(auto& barrier : m_barriers) {
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.subresourceRange = DEFAULT_SUB_RANGE;
    }

    m_barriers[TRANSMITTANCE_BARRIER].image = transmittanceLUT.image;
    m_barriers[IRRADIANCE_BARRIER].image = irradianceLut.image;
    m_barriers[DELTA_RAYLEIGH_BARRIER].image = deltaRayleighScatteringTexture.image;
    m_barriers[DELTA_MIE_BARRIER].image = deltaMieScatteringTexture.image;
    m_barriers[DELTA_IRRADIANCE_BARRIER].image = deltaIrradianceTexture.image;
    m_barriers[DELTA_SCATTERING_DENSITY_BARRIER].image = deltaScatteringDensityTexture.image;
    m_barriers[DELTA_MULTIPLE_DENSITY_BARRIER].image = deltaMultipleScatteringTexture.image;
    m_barriers[SCATTERING_BARRIER].image = scatteringLUT.image;
}

void Atmosphere::refresh() {
    ubo->solarIrradiance = params.solarIrradiance;
    ubo->sunAngularRadius = params.sunAngularRadius;

    ubo->bottomRadius = params.radius.bottom / params.lengthUnitInMeters;
    ubo->topRadius = params.radius.top / params.lengthUnitInMeters;

    ubo->rayleighScattering = params.rayleigh.scattering * params.lengthUnitInMeters;
    ubo->mieScattering = params.mie.scattering * params.lengthUnitInMeters;
    ubo->mieExtinction = params.mie.extinction * params. lengthUnitInMeters;
    ubo->mieAnisotropicFactor = params.mie.anisotropicFactor;

    ubo->absorptionExtinction = params.ozone.absorptionExtinction * params.lengthUnitInMeters;
    ubo->groundAlbedo = params.groundAlbedo;
    ubo->mu_s_min =params. mu_s_min;
    ubo->lengthUnitInMeters = params.lengthUnitInMeters;


    auto& rayleigh_density = layers[DENSITY_PROFILE_RAYLEIGH];
    rayleigh_density.width = 0;
    rayleigh_density.exp_term = 1;
    rayleigh_density.exp_scale = -km / params.rayleigh.height;
    rayleigh_density.linear_term = 0;
    rayleigh_density.constant_term = 0;

    auto& mie_density = layers[DENSITY_PROFILE_MIE];
    mie_density.width = 0;
    mie_density.exp_term = 1;
    mie_density.exp_scale = -km / params.mie.height;
    mie_density.linear_term = 0;
    mie_density.constant_term = 0;

    auto absorption_density = &layers[DENSITY_PROFILE_OZONE];
    absorption_density[BOTTOM].width = params.ozone.bottom.width / params.lengthUnitInMeters;
    absorption_density[BOTTOM].exp_term = 0;
    absorption_density[BOTTOM].exp_scale = 0;
    absorption_density[BOTTOM].linear_term =  km / params.ozone.bottom.linearHeight;
    absorption_density[BOTTOM].constant_term =  params.ozone.bottom.constant;

    absorption_density[TOP].width = 0;
    absorption_density[TOP].exp_term = 0;
    absorption_density[TOP].exp_scale = 0;
    absorption_density[TOP].linear_term =  -km / params.ozone.top.linearHeight;
    absorption_density[TOP].constant_term =  params.ozone.top.constant;
}

void Atmosphere::createPipelines() {
    auto module = m_device->createShaderModule(m_fileMgr->getFullPath("compute_transmittance.comp.spv")->string());
    auto stage = initializers::shaderStage({module, VK_SHADER_STAGE_COMPUTE_BIT});

    pipelines.compute_transmittance.layout = m_device->createPipelineLayout(
            {uboDescriptorSetLayout, imageDescriptorSetLayout});

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = pipelines.compute_transmittance.layout;
    m_device->setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("compute_transmittance_layout",
                                                      pipelines.compute_transmittance.layout.pipelineLayout);

    pipelines.compute_transmittance.pipeline = m_device->createComputePipeline(computeCreateInfo);
    m_device->setName<VK_OBJECT_TYPE_PIPELINE>("compute_transmittance",
                                               pipelines.compute_transmittance.pipeline.handle);


    // compute_direct_irradiance
    module = m_device->createShaderModule(m_fileMgr->getFullPath("compute_direct_irradiance.comp.spv")->string());
    stage = initializers::shaderStage({module, VK_SHADER_STAGE_COMPUTE_BIT});
    pipelines.compute_direct_irradiance.layout  =
            m_device->createPipelineLayout({
                uboDescriptorSetLayout, lutDescriptorSetLayout,
                imageDescriptorSetLayout, tempDescriptorSetLayout
            });
    m_device->setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("compute_direct_irradiance",
                                                      pipelines.compute_direct_irradiance.layout.pipelineLayout);
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = pipelines.compute_direct_irradiance.layout;
    pipelines.compute_direct_irradiance.pipeline = m_device->createComputePipeline(computeCreateInfo);
    m_device->setName<VK_OBJECT_TYPE_PIPELINE>("compute_direct_irradiance",
                                               pipelines.compute_direct_irradiance.pipeline.handle);

    // single scattering
    module = m_device->createShaderModule(m_fileMgr->getFullPath("compute_single_scattering.comp.spv")->string());
    stage = initializers::shaderStage({module, VK_SHADER_STAGE_COMPUTE_BIT});
    pipelines.compute_single_scattering.layout  =
            m_device->createPipelineLayout({
                uboDescriptorSetLayout, lutDescriptorSetLayout,
                imageDescriptorSetLayout, tempDescriptorSetLayout
            });
    m_device->setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("compute_single_scattering",
                                                      pipelines.compute_single_scattering.layout.pipelineLayout);
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = pipelines.compute_single_scattering.layout;
    pipelines.compute_single_scattering.pipeline = m_device->createComputePipeline(computeCreateInfo);
    m_device->setName<VK_OBJECT_TYPE_PIPELINE>("compute_single_scattering",
                                               pipelines.compute_single_scattering.pipeline.handle);

    // scattering density
    module = m_device->createShaderModule(m_fileMgr->getFullPath("compute_scattering_density.comp.spv")->string());
    stage = initializers::shaderStage({module, VK_SHADER_STAGE_COMPUTE_BIT});
    pipelines.compute_scattering_density.layout  =
            m_device->createPipelineLayout(
                    {uboDescriptorSetLayout, lutDescriptorSetLayout, imageDescriptorSetLayout, tempDescriptorSetLayout},
                    {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int)}});
    m_device->setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("compute_scattering_density",
                                                      pipelines.compute_scattering_density.layout.pipelineLayout);
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = pipelines.compute_scattering_density.layout;
    pipelines.compute_scattering_density.pipeline = m_device->createComputePipeline(computeCreateInfo);
    m_device->setName<VK_OBJECT_TYPE_PIPELINE>("compute_scattering_density",
                                               pipelines.compute_scattering_density.pipeline.handle);

    // indirect irradiance
    module = m_device->createShaderModule(m_fileMgr->getFullPath("compute_indirect_irradiance.comp.spv")->string());
    stage = initializers::shaderStage({module, VK_SHADER_STAGE_COMPUTE_BIT});
    pipelines.compute_indirect_irradiance.layout  =
            m_device->createPipelineLayout(
                    {uboDescriptorSetLayout, lutDescriptorSetLayout, imageDescriptorSetLayout, tempDescriptorSetLayout},
                    {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int)}});
    m_device->setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("compute_indirect_irradiance",
                                                      pipelines.compute_indirect_irradiance.layout.pipelineLayout);
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = pipelines.compute_indirect_irradiance.layout;
    pipelines.compute_indirect_irradiance.pipeline = m_device->createComputePipeline(computeCreateInfo);
    m_device->setName<VK_OBJECT_TYPE_PIPELINE>("compute_indirect_irradiance",
                                               pipelines.compute_indirect_irradiance.pipeline.handle);

    // multiple scattering
    module = m_device->createShaderModule(m_fileMgr->getFullPath("compute_multiple_scattering.comp.spv")->string());
    stage = initializers::shaderStage({module, VK_SHADER_STAGE_COMPUTE_BIT});
    pipelines.compute_multiple_scattering.layout  =
            m_device->createPipelineLayout({
                                                   uboDescriptorSetLayout, lutDescriptorSetLayout,
                                                   imageDescriptorSetLayout, tempDescriptorSetLayout
                                           });
    m_device->setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("compute_multiple_scattering",
                                                      pipelines.compute_multiple_scattering.layout.pipelineLayout);
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = pipelines.compute_multiple_scattering.layout;
    pipelines.compute_multiple_scattering.pipeline = m_device->createComputePipeline(computeCreateInfo);
    m_device->setName<VK_OBJECT_TYPE_PIPELINE>("compute_multiple_scattering",
                                               pipelines.compute_multiple_scattering.pipeline.handle);
}



void Atmosphere::computeTransmittanceLUT(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = uboDescriptorSet;
    sets[1] = imageDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_transmittance.pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_transmittance.layout
                            , 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, TRANSMITTANCE_TEXTURE_WIDTH, TRANSMITTANCE_TEXTURE_HEIGHT, 1);
}

void Atmosphere::computeDirectIrradiance(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = uboDescriptorSet;
    sets[1] = lutDescriptorSet;
    sets[2] = imageDescriptorSet;
    sets[3] = tempDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_direct_irradiance.pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_direct_irradiance.layout
            , 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, IRRADIANCE_TEXTURE_WIDTH, IRRADIANCE_TEXTURE_HEIGHT, 1);
}

void Atmosphere::computeSingleScattering(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = uboDescriptorSet;
    sets[1] = lutDescriptorSet;
    sets[2] = imageDescriptorSet;
    sets[3] = tempDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_single_scattering.pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_single_scattering.layout
            , 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, SCATTERING_TEXTURE_WIDTH, SCATTERING_TEXTURE_HEIGHT, SCATTERING_TEXTURE_DEPTH);

}

void Atmosphere::computeScatteringDensity(VkCommandBuffer commandBuffer, int scatteringOrder) {
    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = uboDescriptorSet;
    sets[1] = lutDescriptorSet;
    sets[2] = imageDescriptorSet;
    sets[3] = tempDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_scattering_density.pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_scattering_density.layout
            , 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, pipelines.compute_scattering_density.layout, VK_SHADER_STAGE_COMPUTE_BIT
                       , 0, sizeof(int), &scatteringOrder);
    vkCmdDispatch(commandBuffer, SCATTERING_TEXTURE_WIDTH, SCATTERING_TEXTURE_HEIGHT, SCATTERING_TEXTURE_DEPTH);
}


void Atmosphere::computeIndirectIrradiance(VkCommandBuffer commandBuffer, int scatteringOrder) {
    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = uboDescriptorSet;
    sets[1] = lutDescriptorSet;
    sets[2] = imageDescriptorSet;
    sets[3] = tempDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_indirect_irradiance.pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_indirect_irradiance.layout
            , 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, pipelines.compute_indirect_irradiance.layout, VK_SHADER_STAGE_COMPUTE_BIT
            , 0, sizeof(int), &scatteringOrder);
    vkCmdDispatch(commandBuffer, IRRADIANCE_TEXTURE_WIDTH, IRRADIANCE_TEXTURE_HEIGHT, 1);
}

void Atmosphere::computeMultipleScattering(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = uboDescriptorSet;
    sets[1] = lutDescriptorSet;
    sets[2] = imageDescriptorSet;
    sets[3] = tempDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_multiple_scattering.pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_multiple_scattering.layout
            , 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, SCATTERING_TEXTURE_WIDTH, SCATTERING_TEXTURE_HEIGHT, SCATTERING_TEXTURE_DEPTH);
}

std::function<void()> Atmosphere::ui(Atmosphere &atmosphere) {
    return [&atmosphere]{
        static bool dirty = false;
        auto defaultParams = Params{};
        static auto mieAbsorption = glm::max(glm::vec3(0), atmosphere.params.mie.extinction - atmosphere.params.mie.scattering);
        static auto mieScatteringLength = glm::length(defaultParams.mie.scattering) * km;
        static auto mieAbsorptionLength = glm::length(mieAbsorption) * km;
        static auto rayleighScattingLength = glm::length(defaultParams.rayleigh.scattering) * km;
        static auto ozoneAbsorptionLength = glm::length(defaultParams.ozone.absorptionExtinction) * km;

        ImGui::Begin("Atmosphere");
        ImGui::SetWindowSize({0, 0});
        dirty |= ImGui::SliderFloat("Mie phase", &atmosphere.params.mie.anisotropicFactor, 0, 0.999);
        dirty |= ImGui::SliderInt("Scatt Order", &atmosphere.params.numScatteringOrder, 2, 10);

        static auto mieScattering = atmosphere.params.mie.scattering * km/mieScatteringLength;
        dirty |= ImGui::ColorEdit3("MieScattCoeff", glm::value_ptr(mieScattering));
        dirty |= ImGui::SliderFloat("MieScattScale", &mieScatteringLength, 0.00001f, 0.1f, "%.5f");

        static auto mieAbsorptionColor = mieAbsorption * km/mieAbsorptionLength;
        dirty |= ImGui::ColorEdit3("MieAbsorbCoeff", glm::value_ptr(mieAbsorptionColor));
        dirty |= ImGui::SliderFloat("MieAbsorbScale", &mieAbsorptionLength, 0.00001f, 0.1f, "%.5f");

        static auto rayleighScattering = atmosphere.params.rayleigh.scattering * km/rayleighScattingLength;
        dirty |= ImGui::ColorEdit3( "RayScattCoeff", glm::value_ptr(rayleighScattering));
        dirty |= ImGui::SliderFloat("RayScattScale", &rayleighScattingLength, 0.00001f, 10.0f, "%.5f");

        static auto ozoneAbsorption = atmosphere.params.ozone.absorptionExtinction * km/ozoneAbsorptionLength;
        dirty |= ImGui::ColorEdit3( "AbsorptiCoeff", glm::value_ptr(ozoneAbsorption));
        dirty |= ImGui::SliderFloat("AbsorptiScale", &ozoneAbsorptionLength, 0.00001f, 10.0f, "%.5f");

        static auto planetRadius = atmosphere.params.radius.bottom / atmosphere.params.lengthUnitInMeters;
        static auto atmosphereHeight = (atmosphere.params.radius.top - atmosphere.params.radius.bottom) / atmosphere.params.lengthUnitInMeters;
        dirty |= ImGui::SliderFloat("Planet radius", &planetRadius, 100.0f, 8000.0f);
        dirty |= ImGui::SliderFloat("Atmos height", &atmosphereHeight, 10.0f, 150.0f);

        static auto mieScaleHeight = atmosphere.params.mie.height / atmosphere.params.lengthUnitInMeters;
        static auto rayleighHeight = atmosphere.params.rayleigh.height / atmosphere.params.lengthUnitInMeters;
        dirty |= ImGui::SliderFloat("MieScaleHeight", &mieScaleHeight, 0.5f, 20.0f);
        dirty |= ImGui::SliderFloat("RayleighScaleHeight", &rayleighHeight, 0.5f, 20.0f);

        dirty |= ImGui::ColorEdit3("Ground albedo", glm::value_ptr(atmosphere.params.groundAlbedo));

        ImGui::End();
        if(dirty && !ImGui::IsAnyItemActive()){
            dirty = false;

            atmosphere.params.mie.scattering = mieScattering * mieScatteringLength/km;
            mieAbsorption = mieAbsorptionColor * mieAbsorptionLength/km;
            atmosphere.params.mie.extinction = atmosphere.params.mie.scattering + mieAbsorption;
            atmosphere.params.rayleigh.scattering = rayleighScattering * rayleighScattingLength/km;
            atmosphere.params.ozone.absorptionExtinction = ozoneAbsorption * ozoneAbsorptionLength/km;
            atmosphere.params.radius.bottom = planetRadius * km;
            atmosphere.params.radius.top = (planetRadius + atmosphereHeight) * km;
            atmosphere.params.mie.height = mieScaleHeight * km;
            atmosphere.params.rayleigh.height = rayleighHeight * km;

            atmosphere.generateLUT();
        }
    };
}
