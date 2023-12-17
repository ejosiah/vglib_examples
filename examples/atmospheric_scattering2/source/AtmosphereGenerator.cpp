#include "AtmosphereGenerator.hpp"
#include "glm_format.h"
#include <spdlog/spdlog.h>
#include "Texture.h"
#include <array>
#include <imgui.h>
#include "Atmosphere.hpp"

AtmosphereGenerator::AtmosphereGenerator(VulkanDevice* device, VulkanDescriptorPool* descriptorPool, FileManager* fileManager)
: m_device{device},
  m_descriptorPool{ descriptorPool },
  m_fileMgr{ fileManager }
{
   initAtmosphereDescriptor();
   createSampler();
   createTextures();
   createBarriers();
   refresh();
   createDescriptorSetLayout();
   updateDescriptorSet();
   createPipelines();
   spdlog::info("size of DensityProfileLayer: {}", sizeof(Atmosphere::DensityProfileLayer));
}

void AtmosphereGenerator::generateLUT() {
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

void AtmosphereGenerator::initAtmosphereDescriptor() {
    m_atmosphereDescriptor = AtmosphereDescriptor{ m_device, m_descriptorPool};
    m_atmosphereDescriptor.init();
}

void AtmosphereGenerator::barrier(VkCommandBuffer commandBuffer, const std::vector<int>& images) {
    std::vector<VkImageMemoryBarrier> barriers{};
    for(auto image : images) barriers.push_back(m_barriers[image]);

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            , 0, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, COUNT(barriers), barriers.data());
}

void AtmosphereGenerator::createSampler() {
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

void AtmosphereGenerator::createTextures() {
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


    deltaIrradianceTexture.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    deltaRayleighScatteringTexture.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    deltaMieScatteringTexture.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    deltaScatteringDensityTexture.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    deltaMultipleScatteringTexture.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
}

void AtmosphereGenerator::createDescriptorSetLayout() {
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

    auto sets = m_descriptorPool->allocate({imageDescriptorSetLayout, tempDescriptorSetLayout});

    imageDescriptorSet = sets[0];
    tempDescriptorSet = sets[1];
}

void AtmosphereGenerator::updateDescriptorSet() {
    
    auto writes = initializers::writeDescriptorSets<4>();

    // images
    writes[0].dstSet = imageDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo irradianceInfo{ VK_NULL_HANDLE, irradianceLut().imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[0].pImageInfo = &irradianceInfo;

    writes[1].dstSet = imageDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo transmittanceInfo{ VK_NULL_HANDLE, transmittanceLUT().imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[1].pImageInfo = &transmittanceInfo;

    writes[2].dstSet = imageDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo scatteringInfo{ VK_NULL_HANDLE, scatteringLUT().imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &scatteringInfo;

    writes[3] = writes[2];
    writes[3].dstBinding = 3;
    m_device->updateDescriptorSets(writes);

    // temp writes
    writes = initializers::writeDescriptorSets<10>();
    writes[0].dstSet = tempDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo deltaIrradianceInfo{VK_NULL_HANDLE, deltaIrradianceTexture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[0].pImageInfo = &deltaIrradianceInfo;

    writes[1].dstSet = tempDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo deltaRayleighInfo{VK_NULL_HANDLE, deltaRayleighScatteringTexture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[1].pImageInfo = &deltaRayleighInfo;

    writes[2].dstSet = tempDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo deltaMieInfo{VK_NULL_HANDLE, deltaMieScatteringTexture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &deltaMieInfo;

    writes[3].dstSet = tempDescriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo deltaScatteringInfo{VK_NULL_HANDLE, deltaScatteringDensityTexture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[3].pImageInfo = &deltaScatteringInfo;

    writes[4].dstSet = tempDescriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[4].descriptorCount = 1;
    VkDescriptorImageInfo deltaMultipleScatteringInfo{VK_NULL_HANDLE, deltaMultipleScatteringTexture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
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

void AtmosphereGenerator::createBarriers() {
    m_barriers.resize(NUM_BARRIERS);
    for(auto& barrier : m_barriers) {
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.subresourceRange = DEFAULT_SUB_RANGE;
    }

    m_barriers[TRANSMITTANCE_BARRIER].image = transmittanceLUT().image;
    m_barriers[IRRADIANCE_BARRIER].image = irradianceLut().image;
    m_barriers[DELTA_RAYLEIGH_BARRIER].image = deltaRayleighScatteringTexture.image;
    m_barriers[DELTA_MIE_BARRIER].image = deltaMieScatteringTexture.image;
    m_barriers[DELTA_IRRADIANCE_BARRIER].image = deltaIrradianceTexture.image;
    m_barriers[DELTA_SCATTERING_DENSITY_BARRIER].image = deltaScatteringDensityTexture.image;
    m_barriers[DELTA_MULTIPLE_DENSITY_BARRIER].image = deltaMultipleScatteringTexture.image;
    m_barriers[SCATTERING_BARRIER].image = scatteringLUT().image;
}

void AtmosphereGenerator::refresh() {
    ubo()->solarIrradiance = params.solarIrradiance;
    ubo()->sunAngularRadius = params.sunAngularRadius;

    ubo()->bottomRadius = params.radius.bottom / params.lengthUnitInMeters;
    ubo()->topRadius = params.radius.top / params.lengthUnitInMeters;

    ubo()->rayleighScattering = params.rayleigh.scattering * params.lengthUnitInMeters;
    ubo()->mieScattering = params.mie.scattering * params.lengthUnitInMeters;
    ubo()->mieExtinction = params.mie.extinction * params. lengthUnitInMeters;
    ubo()->mieAnisotropicFactor = params.mie.anisotropicFactor;

    ubo()->absorptionExtinction = params.ozone.absorptionExtinction * params.lengthUnitInMeters;
    ubo()->groundAlbedo = params.groundAlbedo;
    ubo()->mu_s_min =params. mu_s_min;
    ubo()->lengthUnitInMeters = params.lengthUnitInMeters;


    auto& rayleigh_density = layers()[DENSITY_PROFILE_RAYLEIGH];
    rayleigh_density.width = 0;
    rayleigh_density.exp_term = 1;
    rayleigh_density.exp_scale = -km / params.rayleigh.height;
    rayleigh_density.linear_term = 0;
    rayleigh_density.constant_term = 0;

    auto& mie_density = layers()[DENSITY_PROFILE_MIE];
    mie_density.width = 0;
    mie_density.exp_term = 1;
    mie_density.exp_scale = -km / params.mie.height;
    mie_density.linear_term = 0;
    mie_density.constant_term = 0;

    auto absorption_density = &layers()[DENSITY_PROFILE_OZONE];
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

void AtmosphereGenerator::createPipelines() {
    auto module = m_device->createShaderModule(m_fileMgr->getFullPath("compute_transmittance.comp.spv")->string());
    auto stage = initializers::shaderStage({module, VK_SHADER_STAGE_COMPUTE_BIT});

    pipelines.compute_transmittance.layout = m_device->createPipelineLayout(
            {uboDescriptorSetLayout(), imageDescriptorSetLayout});

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = pipelines.compute_transmittance.layout.handle;
    m_device->setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("compute_transmittance_layout",
                                                      pipelines.compute_transmittance.layout.handle);

    pipelines.compute_transmittance.pipeline = m_device->createComputePipeline(computeCreateInfo);
    m_device->setName<VK_OBJECT_TYPE_PIPELINE>("compute_transmittance",
                                               pipelines.compute_transmittance.pipeline.handle);


    // compute_direct_irradiance
    module = m_device->createShaderModule(m_fileMgr->getFullPath("compute_direct_irradiance.comp.spv")->string());
    stage = initializers::shaderStage({module, VK_SHADER_STAGE_COMPUTE_BIT});
    pipelines.compute_direct_irradiance.layout  =
            m_device->createPipelineLayout({
                uboDescriptorSetLayout(), lutDescriptorSetLayout(),
                imageDescriptorSetLayout, tempDescriptorSetLayout
            });
    m_device->setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("compute_direct_irradiance",
                                                      pipelines.compute_direct_irradiance.layout.handle);
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = pipelines.compute_direct_irradiance.layout.handle;
    pipelines.compute_direct_irradiance.pipeline = m_device->createComputePipeline(computeCreateInfo);
    m_device->setName<VK_OBJECT_TYPE_PIPELINE>("compute_direct_irradiance",
                                               pipelines.compute_direct_irradiance.pipeline.handle);

    // single scattering
    module = m_device->createShaderModule(m_fileMgr->getFullPath("compute_single_scattering.comp.spv")->string());
    stage = initializers::shaderStage({module, VK_SHADER_STAGE_COMPUTE_BIT});
    pipelines.compute_single_scattering.layout  =
            m_device->createPipelineLayout({
                uboDescriptorSetLayout(), lutDescriptorSetLayout(),
                imageDescriptorSetLayout, tempDescriptorSetLayout
            });
    m_device->setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("compute_single_scattering",
                                                      pipelines.compute_single_scattering.layout.handle);
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = pipelines.compute_single_scattering.layout.handle;
    pipelines.compute_single_scattering.pipeline = m_device->createComputePipeline(computeCreateInfo);
    m_device->setName<VK_OBJECT_TYPE_PIPELINE>("compute_single_scattering",
                                               pipelines.compute_single_scattering.pipeline.handle);

    // scattering density
    module = m_device->createShaderModule(m_fileMgr->getFullPath("compute_scattering_density.comp.spv")->string());
    stage = initializers::shaderStage({module, VK_SHADER_STAGE_COMPUTE_BIT});
    pipelines.compute_scattering_density.layout  =
            m_device->createPipelineLayout(
                    {uboDescriptorSetLayout(), lutDescriptorSetLayout(), imageDescriptorSetLayout, tempDescriptorSetLayout},
                    {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int)}});
    m_device->setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("compute_scattering_density",
                                                      pipelines.compute_scattering_density.layout.handle);
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = pipelines.compute_scattering_density.layout.handle;
    pipelines.compute_scattering_density.pipeline = m_device->createComputePipeline(computeCreateInfo);
    m_device->setName<VK_OBJECT_TYPE_PIPELINE>("compute_scattering_density",
                                               pipelines.compute_scattering_density.pipeline.handle);

    // indirect irradiance
    module = m_device->createShaderModule(m_fileMgr->getFullPath("compute_indirect_irradiance.comp.spv")->string());
    stage = initializers::shaderStage({module, VK_SHADER_STAGE_COMPUTE_BIT});
    pipelines.compute_indirect_irradiance.layout  =
            m_device->createPipelineLayout(
                    {uboDescriptorSetLayout(), lutDescriptorSetLayout(), imageDescriptorSetLayout, tempDescriptorSetLayout},
                    {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int)}});
    m_device->setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("compute_indirect_irradiance",
                                                      pipelines.compute_indirect_irradiance.layout.handle);
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = pipelines.compute_indirect_irradiance.layout.handle;
    pipelines.compute_indirect_irradiance.pipeline = m_device->createComputePipeline(computeCreateInfo);
    m_device->setName<VK_OBJECT_TYPE_PIPELINE>("compute_indirect_irradiance",
                                               pipelines.compute_indirect_irradiance.pipeline.handle);

    // multiple scattering
    module = m_device->createShaderModule(m_fileMgr->getFullPath("compute_multiple_scattering.comp.spv")->string());
    stage = initializers::shaderStage({module, VK_SHADER_STAGE_COMPUTE_BIT});
    pipelines.compute_multiple_scattering.layout  =
            m_device->createPipelineLayout({
                                                   uboDescriptorSetLayout(), lutDescriptorSetLayout(),
                                                   imageDescriptorSetLayout, tempDescriptorSetLayout
                                           });
    m_device->setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("compute_multiple_scattering",
                                                      pipelines.compute_multiple_scattering.layout.handle);
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = pipelines.compute_multiple_scattering.layout.handle;
    pipelines.compute_multiple_scattering.pipeline = m_device->createComputePipeline(computeCreateInfo);
    m_device->setName<VK_OBJECT_TYPE_PIPELINE>("compute_multiple_scattering",
                                               pipelines.compute_multiple_scattering.pipeline.handle);
}



void AtmosphereGenerator::computeTransmittanceLUT(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = uboDescriptorSet();
    sets[1] = imageDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_transmittance.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_transmittance.layout.handle
                            , 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, TRANSMITTANCE_TEXTURE_WIDTH, TRANSMITTANCE_TEXTURE_HEIGHT, 1);
}

void AtmosphereGenerator::computeDirectIrradiance(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = uboDescriptorSet();
    sets[1] = lutDescriptorSet();
    sets[2] = imageDescriptorSet;
    sets[3] = tempDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_direct_irradiance.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_direct_irradiance.layout.handle
            , 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, IRRADIANCE_TEXTURE_WIDTH, IRRADIANCE_TEXTURE_HEIGHT, 1);
}

void AtmosphereGenerator::computeSingleScattering(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = uboDescriptorSet();
    sets[1] = lutDescriptorSet();
    sets[2] = imageDescriptorSet;
    sets[3] = tempDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_single_scattering.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_single_scattering.layout.handle
            , 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, SCATTERING_TEXTURE_WIDTH, SCATTERING_TEXTURE_HEIGHT, SCATTERING_TEXTURE_DEPTH);

}

void AtmosphereGenerator::computeScatteringDensity(VkCommandBuffer commandBuffer, int scatteringOrder) {
    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = uboDescriptorSet();
    sets[1] = lutDescriptorSet();
    sets[2] = imageDescriptorSet;
    sets[3] = tempDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_scattering_density.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_scattering_density.layout.handle
            , 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, pipelines.compute_scattering_density.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT
                       , 0, sizeof(int), &scatteringOrder);
    vkCmdDispatch(commandBuffer, SCATTERING_TEXTURE_WIDTH, SCATTERING_TEXTURE_HEIGHT, SCATTERING_TEXTURE_DEPTH);
}


void AtmosphereGenerator::computeIndirectIrradiance(VkCommandBuffer commandBuffer, int scatteringOrder) {
    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = uboDescriptorSet();
    sets[1] = lutDescriptorSet();
    sets[2] = imageDescriptorSet;
    sets[3] = tempDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_indirect_irradiance.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_indirect_irradiance.layout.handle
            , 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, pipelines.compute_indirect_irradiance.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT
            , 0, sizeof(int), &scatteringOrder);
    vkCmdDispatch(commandBuffer, IRRADIANCE_TEXTURE_WIDTH, IRRADIANCE_TEXTURE_HEIGHT, 1);
}

void AtmosphereGenerator::computeMultipleScattering(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = uboDescriptorSet();
    sets[1] = lutDescriptorSet();
    sets[2] = imageDescriptorSet;
    sets[3] = tempDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_multiple_scattering.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.compute_multiple_scattering.layout.handle
            , 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, SCATTERING_TEXTURE_WIDTH, SCATTERING_TEXTURE_HEIGHT, SCATTERING_TEXTURE_DEPTH);
}

std::function<void()> AtmosphereGenerator::ui() {
    return [this]{
        static bool dirty = false;
        auto defaultParams = Params{};
        static auto mieAbsorption = glm::max(glm::vec3(0), params.mie.extinction - params.mie.scattering);
        static auto mieScatteringLength = glm::length(defaultParams.mie.scattering) * km;
        static auto mieAbsorptionLength = glm::length(mieAbsorption) * km;
        static auto rayleighScattingLength = glm::length(defaultParams.rayleigh.scattering) * km;
        static auto ozoneAbsorptionLength = glm::length(defaultParams.ozone.absorptionExtinction) * km;

        ImGui::Begin("AtmosphereGenerator");
        ImGui::SetWindowSize({0, 0});
        dirty |= ImGui::SliderFloat("Mie phase", &params.mie.anisotropicFactor, 0, 0.999);
        dirty |= ImGui::SliderInt("Scatt Order", &params.numScatteringOrder, 2, 10);

        static auto mieScattering = params.mie.scattering * km/mieScatteringLength;
        dirty |= ImGui::ColorEdit3("MieScattCoeff", glm::value_ptr(mieScattering));
        dirty |= ImGui::SliderFloat("MieScattScale", &mieScatteringLength, 0.00001f, 0.1f, "%.5f");

        static auto mieAbsorptionColor = mieAbsorption * km/mieAbsorptionLength;
        dirty |= ImGui::ColorEdit3("MieAbsorbCoeff", glm::value_ptr(mieAbsorptionColor));
        dirty |= ImGui::SliderFloat("MieAbsorbScale", &mieAbsorptionLength, 0.00001f, 0.1f, "%.5f");

        static auto rayleighScattering = params.rayleigh.scattering * km/rayleighScattingLength;
        dirty |= ImGui::ColorEdit3( "RayScattCoeff", glm::value_ptr(rayleighScattering));
        dirty |= ImGui::SliderFloat("RayScattScale", &rayleighScattingLength, 0.00001f, 10.0f, "%.5f");

        static auto ozoneAbsorption = params.ozone.absorptionExtinction * km/ozoneAbsorptionLength;
        dirty |= ImGui::ColorEdit3( "AbsorptiCoeff", glm::value_ptr(ozoneAbsorption));
        dirty |= ImGui::SliderFloat("AbsorptiScale", &ozoneAbsorptionLength, 0.00001f, 10.0f, "%.5f");

        static auto planetRadius = params.radius.bottom / params.lengthUnitInMeters;
        static auto atmosphereHeight = (params.radius.top - params.radius.bottom) / params.lengthUnitInMeters;
        dirty |= ImGui::SliderFloat("Planet radius", &planetRadius, 100.0f, 8000.0f);
        dirty |= ImGui::SliderFloat("Atmos height", &atmosphereHeight, 10.0f, 150.0f);

        static auto mieScaleHeight = params.mie.height / params.lengthUnitInMeters;
        static auto rayleighHeight = params.rayleigh.height / params.lengthUnitInMeters;
        dirty |= ImGui::SliderFloat("MieScaleHeight", &mieScaleHeight, 0.5f, 20.0f);
        dirty |= ImGui::SliderFloat("RayleighScaleHeight", &rayleighHeight, 0.5f, 20.0f);

        dirty |= ImGui::ColorEdit3("Ground albedo", glm::value_ptr(params.groundAlbedo));

        if(ImGui::Button("Save")) {
            save("default.atmosphere");
        }

        ImGui::End();
        if(dirty && !ImGui::IsAnyItemActive()){
            dirty = false;

            params.mie.scattering = mieScattering * mieScatteringLength/km;
            mieAbsorption = mieAbsorptionColor * mieAbsorptionLength/km;
            params.mie.extinction = params.mie.scattering + mieAbsorption;
            params.rayleigh.scattering = rayleighScattering * rayleighScattingLength/km;
            params.ozone.absorptionExtinction = ozoneAbsorption * ozoneAbsorptionLength/km;
            params.radius.bottom = planetRadius * km;
            params.radius.top = (planetRadius + atmosphereHeight) * km;
            params.mie.height = mieScaleHeight * km;
            params.rayleigh.height = rayleighHeight * km;

            generateLUT();
        }
    };
}


void AtmosphereGenerator::save(const std::filesystem::path& path) {
    Atmosphere::Header header{
        .solarIrradiance = ubo()->solarIrradiance,
        .rayleighScattering = ubo()->rayleighScattering,
        .mieScattering = ubo()->mieScattering,
        .mieExtinction = ubo()->mieExtinction,
        .absorptionExtinction = ubo()->absorptionExtinction,
        .groundAlbedo = ubo()->groundAlbedo,
        .sunAngularRadius = ubo()->sunAngularRadius,
        .bottomRadius = ubo()->bottomRadius,
        .topRadius = ubo()->topRadius,
        .mu_s_min = ubo()->mu_s_min,
        .mieAnisotropicFactor = ubo()->mieAnisotropicFactor,
        .lengthUnitInMeters = ubo()->lengthUnitInMeters
    };


    std::ofstream fout{path.string(), std::ios::binary};
    if(!fout.good()){
        spdlog::error("unable to save atmosphere to file {}", path.string());
        return;
    }

    VulkanBuffer staging = m_device->createStagingBuffer(DATA_SIZE);
    // copy into staging buffer;
    
    m_device->graphicsCommandPool().oneTimeCommand([&](auto cmdBuffer){
       std::array<VkImageMemoryBarrier, 3>  barriers{};
       
       for(auto& barrier : barriers) {
           barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
           barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
           barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
           barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
           barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
           barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
           barrier.subresourceRange.baseMipLevel = 0;
           barrier.subresourceRange.levelCount = 1;
           barrier.subresourceRange.baseArrayLayer = 0;
           barrier.subresourceRange.layerCount = 1;
       }
       
       barriers[0].image = irradianceLut().image.image;
       barriers[1].image = transmittanceLUT().image.image;
       barriers[2].image = scatteringLUT().image.image;

        vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                             , VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, barriers.size(), barriers.data());

        VkBufferImageCopy region{0, 0, 0};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = { IRRADIANCE_TEXTURE_WIDTH, IRRADIANCE_TEXTURE_HEIGHT, 1 };
        vkCmdCopyImageToBuffer(cmdBuffer, irradianceLut().image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &region);

        region.bufferOffset = IRRADIANCE_TEXTURE_WIDTH * IRRADIANCE_TEXTURE_HEIGHT * NUM_CHANNELS * sizeof(float);
        region.imageExtent = {TRANSMITTANCE_TEXTURE_WIDTH, TRANSMITTANCE_TEXTURE_HEIGHT, 1};
        vkCmdCopyImageToBuffer(cmdBuffer, transmittanceLUT().image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &region);

        region.bufferOffset += TRANSMITTANCE_TEXTURE_WIDTH * TRANSMITTANCE_TEXTURE_HEIGHT * NUM_CHANNELS * sizeof(float);
        region.imageExtent = { SCATTERING_TEXTURE_WIDTH, SCATTERING_TEXTURE_HEIGHT, SCATTERING_TEXTURE_DEPTH};
        vkCmdCopyImageToBuffer(cmdBuffer, scatteringLUT().image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &region);

        for(auto& barrier : barriers) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        }

        VkMemoryBarrier memoryBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};

        vkCmdPipelineBarrier(cmdBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT
                             , VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT
                             , 0, 1, &memoryBarrier, 0, nullptr, barriers.size(), barriers.data());
    });

    fout.write(reinterpret_cast<char*>(&header.scatteringDimensions), sizeof(Dim3));
    fout.write(reinterpret_cast<char*>(&header.transmittanceDimensions), sizeof(Dim2));
    fout.write(reinterpret_cast<char*>(&header.irradianceDimensions), sizeof(Dim2));

    fout.write(reinterpret_cast<char*>(&header.solarIrradiance), sizeof(float3));
    fout.write(reinterpret_cast<char*>(&header.rayleighScattering), sizeof(float3));
    fout.write(reinterpret_cast<char*>(&header.mieScattering), sizeof(float3));
    fout.write(reinterpret_cast<char*>(&header.mieExtinction), sizeof(float3));
    fout.write(reinterpret_cast<char*>(&header.groundAlbedo), sizeof(float3));

    fout.write(reinterpret_cast<char*>(&header.sunAngularRadius), sizeof(float));
    fout.write(reinterpret_cast<char*>(&header.bottomRadius), sizeof(float));
    fout.write(reinterpret_cast<char*>(&header.topRadius), sizeof(float));
    fout.write(reinterpret_cast<char*>(&header.mu_s_min), sizeof(float));
    fout.write(reinterpret_cast<char*>(&header.mieAnisotropicFactor), sizeof(float));
    fout.write(reinterpret_cast<char*>(&header.lengthUnitInMeters), sizeof(float));

    fout.write(reinterpret_cast<char*>(staging.map()), DATA_SIZE);
    staging.unmap();

    spdlog::info("atmosphere written to file: {}", path.string());
}

const AtmosphereDescriptor &AtmosphereGenerator::atmosphereDescriptor() const {
    return m_atmosphereDescriptor;
}

Texture &AtmosphereGenerator::transmittanceLUT() {
    return m_atmosphereDescriptor.transmittanceLUT;
}

Texture &AtmosphereGenerator::irradianceLut() {
    return m_atmosphereDescriptor.irradianceLut;
}

Texture &AtmosphereGenerator::scatteringLUT() {
    return m_atmosphereDescriptor.scatteringLUT;
}

AtmosphereDescriptor::UBO *AtmosphereGenerator::ubo() {
    return m_atmosphereDescriptor.ubo;
}

Atmosphere::DensityProfileLayer *AtmosphereGenerator::layers() {
    return m_atmosphereDescriptor.layers;
}

VulkanDescriptorSetLayout &AtmosphereGenerator::uboDescriptorSetLayout() {
    return m_atmosphereDescriptor.uboDescriptorSetLayout;
}

VkDescriptorSet AtmosphereGenerator::uboDescriptorSet() {
    return m_atmosphereDescriptor.uboDescriptorSet;
}

VulkanDescriptorSetLayout &AtmosphereGenerator::lutDescriptorSetLayout() {
    return m_atmosphereDescriptor.lutDescriptorSetLayout;
}

VkDescriptorSet AtmosphereGenerator::lutDescriptorSet() {
    return m_atmosphereDescriptor.lutDescriptorSet;
}
