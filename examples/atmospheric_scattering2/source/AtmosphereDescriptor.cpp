#include "AtmosphereDescriptor.hpp"

AtmosphereDescriptor::AtmosphereDescriptor(VulkanDevice *device, VulkanDescriptorPool *m_descriptorPool)
: m_device{ device }
, m_descriptorPool{ m_descriptorPool }
{}

void AtmosphereDescriptor::init() {
    createBuffers();
    createSampler();
    createLutTextures();
    createDescriptorSetLayout();
    updateDescriptorSet();
}

void AtmosphereDescriptor::createBuffers() {
    m_uboBuffer = m_device->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(UBO), "atmosphere_params");
    m_densityProfileBuffer = m_device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(Atmosphere::DensityProfileLayer) * 4, "density_profile_layers");

    ubo = reinterpret_cast<UBO*>(m_uboBuffer.map());
    layers = reinterpret_cast<Atmosphere::DensityProfileLayer*>(m_densityProfileBuffer.map());
}

void AtmosphereDescriptor::createSampler() {
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

void AtmosphereDescriptor::createLutTextures() {
    textures::create(*m_device, transmittanceLUT, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT
            , {TRANSMITTANCE_TEXTURE_WIDTH, TRANSMITTANCE_TEXTURE_HEIGHT, 1}
            , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float ));

    textures::create(*m_device, irradianceLut, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT
            , {IRRADIANCE_TEXTURE_WIDTH, IRRADIANCE_TEXTURE_HEIGHT, 1}
            , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float ));

    textures::create(*m_device, scatteringLUT, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT
            , {SCATTERING_TEXTURE_WIDTH, SCATTERING_TEXTURE_HEIGHT, SCATTERING_TEXTURE_DEPTH}
            , VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float ));

    transmittanceLUT.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    irradianceLut.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    scatteringLUT.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
}

void AtmosphereDescriptor::createDescriptorSetLayout() {
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

    auto sets = m_descriptorPool->allocate({uboDescriptorSetLayout, lutDescriptorSetLayout});
    uboDescriptorSet = sets[0];
    lutDescriptorSet = sets[1];

}

void AtmosphereDescriptor::updateDescriptorSet() {
    auto writes = initializers::writeDescriptorSets<2>();

    writes[0].dstSet = uboDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uboInfo{m_uboBuffer, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &uboInfo;

    VkDeviceSize DensityProfileLayerSize = sizeof(Atmosphere::DensityProfileLayer);
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

    writes[0].dstSet = lutDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo irradianceInfo{ VK_NULL_HANDLE, irradianceLut.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[0].pImageInfo = &irradianceInfo;

    writes[1].dstSet = lutDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo transmittanceInfo{ VK_NULL_HANDLE, transmittanceLUT.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[1].pImageInfo = &transmittanceInfo;

    writes[2].dstSet = lutDescriptorSet ;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo scatteringInfo{ VK_NULL_HANDLE, scatteringLUT.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &scatteringInfo;

    // single_mie_scattering
    writes[3] = writes[2];
    writes[3].dstBinding = 3;
    m_device->updateDescriptorSets(writes);
}

void AtmosphereDescriptor::load(const std::filesystem::path &path) {

}