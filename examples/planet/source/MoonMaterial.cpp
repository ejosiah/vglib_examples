#include "MoonMaterial.hpp"

#include "AppContext.hpp"
#include "Barrier.hpp"
#include "descriptor_utils.hpp"
#include "filemanager.hpp"

namespace {
    constexpr auto EvaluateSurfaceGradient = "EvaluateSurfaceGradient";
    constexpr auto EvaluateDetailSlope = "EvaluateDetailSlope";
    constexpr uint32_t WorkgroupResolution = 8;

    bool descriptorSetLayoutCreated = false;

    uint32_t workgroup_count(uint32_t value) {
        return nearestMultiple(value, WorkgroupResolution) / WorkgroupResolution;
    }

    void transition_to_general(const VulkanDevice& device, Texture& texture) {
        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, texture.levels, 0, texture.layers };
        texture.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL, range);
    }
}

VulkanDescriptorSetLayout MoonMaterial::descriptorSetLayout;

MoonMaterial::MoonMaterial(VulkanDevice &device)
: m_device(&device) {}

void MoonMaterial::initialize(const Planet &moon) {
    m_moon = &moon;
    loadTextures();
    createSamplers();
    createBuffers();
    upload_constant_buffers();
    createDescriptorSet();
    updateDescriptorSets();
    createPipelines();
    prepareRendering();
}

void MoonMaterial::prepareRendering() {
    m_device->graphicsCommandPool().oneTimeCommand([&](auto cmd) {
        const std::array descriptorSets{ m_descriptorSet };
        const auto elevationLayout = m_compute.layout(EvaluateSurfaceGradient);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(EvaluateSurfaceGradient));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, elevationLayout, 0, descriptorSets.size(), descriptorSets.data(), 0, nullptr);
        vkCmdDispatch(cmd, workgroup_count(m_ElevationTexture.width), workgroup_count(m_ElevationTexture.height), 1);
        Barrier::computeWriteToRead(cmd);

        const auto detailLayout = m_compute.layout(EvaluateDetailSlope);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(EvaluateDetailSlope));
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, detailLayout, 0, descriptorSets.size(), descriptorSets.data(), 0, nullptr);
        vkCmdDispatch(cmd, workgroup_count(m_DetailTexture.width), workgroup_count(m_DetailTexture.height), 1);
        Barrier::computeWriteToRead(cmd);

        textures::generateLOD(cmd, m_AlbedoTexture.image, m_AlbedoTexture.width, m_AlbedoTexture.height, m_AlbedoTexture.levels);
        textures::generateLOD(cmd, m_ElevationTexture.image, m_ElevationTexture.width, m_ElevationTexture.height, m_ElevationTexture.levels);
        textures::generateLOD(cmd, m_ElevationSGTexture.image, m_ElevationSGTexture.width, m_ElevationSGTexture.height, m_ElevationSGTexture.levels);
        textures::generateLOD(cmd, m_DetailTexture.image, m_DetailTexture.width, m_DetailTexture.height, m_DetailTexture.levels);
        textures::generateLOD(cmd, m_DetailSGTexture.image, m_DetailSGTexture.width, m_DetailSGTexture.height, m_DetailSGTexture.levels);
    });
}

void MoonMaterial::createSamplers() {
    VkSamplerCreateInfo samplerInfo = initializers::samplerCreateInfo();
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(m_AlbedoTexture.levels - 1);

    m_LinearRepeatSampler = m_device->createSampler(samplerInfo);

    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    m_LinearMirrorVSampler = m_device->createSampler(samplerInfo);
}

void MoonMaterial::createBuffers() {
    m_MoonCB.gpu = m_device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(MoonCB), "moon_material_cb");
    m_MoonCB.cpu = static_cast<MoonCB*>(m_MoonCB.gpu.map());
}

void MoonMaterial::upload_constant_buffers() {
    *m_MoonCB.cpu = MoonCB{
        .ElevationTextureSize = { m_ElevationTexture.width, m_ElevationTexture.height },
        .DetailTextureSize = { m_DetailTexture.width, m_DetailTexture.height },
        .PatchSize = m_PatchSize,
        .PatchAmplitude = m_PatchAmplitude,
        .NumOctaves = m_NumOctaves,
        .Attenuation = m_Attenuation ? 1u : 0u,
    };
}

void MoonMaterial::loadTextures() {
    m_AlbedoTexture.levels = 5;
    m_ElevationTexture.levels = 5;
    m_DetailTexture.levels = 5;
    m_ElevationSGTexture.levels = 5;
    m_DetailSGTexture.levels = 5;

    textures::fromFile(*m_device, m_AlbedoTexture, FileManager::resource("albedo.png"));

    textures::fromFile(*m_device, m_ElevationTexture, FileManager::resource("elevation.hdr"));
    textures::create(*m_device, m_ElevationSGTexture, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT,
                     {m_ElevationTexture.width, m_ElevationTexture.height, 1});
    transition_to_general(*m_device, m_ElevationSGTexture);

    textures::fromFile(*m_device, m_DetailTexture, FileManager::resource("simplex.png"));
    textures::create(*m_device, m_DetailSGTexture, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16_SFLOAT,
                     {m_DetailTexture.width, m_DetailTexture.height, 1});
    transition_to_general(*m_device, m_DetailSGTexture);

    const VkImageSubresourceRange mip0Range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    m_ElevationSGMip0View =
        m_ElevationSGTexture.image.createView(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, mip0Range);
    m_DetailSGMip0View =
        m_DetailSGTexture.image.createView(VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, mip0Range);

}

void MoonMaterial::createPipelines() {
    m_compute = ComputePipelines{
        m_device,
        {
            {
                .name = EvaluateSurfaceGradient,
                .shadePath = FileManager::resource("moon_evaluate_surface_gradient.comp.spv"),
                .layouts = { &descriptorSetLayout },
            },
            {
                .name = EvaluateDetailSlope,
                .shadePath = FileManager::resource("moon_evaluate_detail_slope.comp.spv"),
                .layouts = { &descriptorSetLayout },
            },
        },
    };
    m_compute.createPipelines();
}

void MoonMaterial::createDescriptorSet() {
    createDescriptorSet(*m_device, m_LinearRepeatSampler, m_LinearMirrorVSampler);
}

void MoonMaterial::createDescriptorSet(VulkanDevice &device, const VulkanSampler& linearRepeatSampler, const VulkanSampler& linearMirrorVSampler) {
    if (descriptorSetLayoutCreated) return;

    descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("moon_material_descriptor_set_layout")
            .binding(0) // m_AlbedoTexture
                .descriptorType(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1) // m_ElevationTexture
                .descriptorType(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(2) // m_DetailTexture
                .descriptorType(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(3) // m_ElevationSGTexture
                .descriptorType(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(4) // m_DetailSGTexture
                .descriptorType(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(5) // m_ElevationSGTexture
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(6) // m_DetailSGTexture
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(7) // m_MoonCB
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(8) // moon PlanetCB
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(9) // repeat sampler for deformation and rendering
                .descriptorType(VK_DESCRIPTOR_TYPE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
                .immutableSamplers(linearRepeatSampler)
            .binding(10) // mirror-V sampler for surface-gradient preparation
                .descriptorType(VK_DESCRIPTOR_TYPE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
                .immutableSamplers(linearMirrorVSampler)
        .createLayout();

    descriptorSetLayoutCreated = true;
}

void MoonMaterial::updateDescriptorSets() {
    m_descriptorSet = AppContext::descriptorPool().allocate({ descriptorSetLayout }).front();
    m_device->setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("moon_material_descriptor_set", m_descriptorSet);

    const VkDescriptorBufferInfo moonCBInfo = descriptor_buffer_info(m_MoonCB.gpu);
    const VkDescriptorBufferInfo planetCBInfo = descriptor_buffer_info(m_moon->m_PlanetCB);

    auto AlbedoInfo = descriptor_image_info(m_AlbedoTexture.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    auto ElevationInfo = descriptor_image_info(m_ElevationTexture.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    auto DetailInfo = descriptor_image_info(m_DetailTexture.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    auto ElevationSGInfo = descriptor_image_info(m_ElevationSGTexture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL);
    auto DetailSGInfo = descriptor_image_info(m_DetailSGTexture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL);
    auto ElevationSGStorageInfo = descriptor_image_info(m_ElevationSGMip0View.handle, VK_IMAGE_LAYOUT_GENERAL);
    auto DetailSGStorageInfo = descriptor_image_info(m_DetailSGMip0View.handle, VK_IMAGE_LAYOUT_GENERAL);

    auto writes = initializers::writeDescriptorSets<9>(m_descriptorSet);

    set_image_write(writes[0], 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &AlbedoInfo, 1);
    set_image_write(writes[1], 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &ElevationInfo, 1);
    set_image_write(writes[2], 2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &DetailInfo, 1);
    set_image_write(writes[3], 3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &ElevationSGInfo, 1);
    set_image_write(writes[4], 4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &DetailSGInfo, 1);
    set_image_write(writes[5], 5, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &ElevationSGStorageInfo, 1);
    set_image_write(writes[6], 6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &DetailSGStorageInfo, 1);
    set_buffer_write(writes[7], 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &moonCBInfo);
    set_buffer_write(writes[8], 8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &planetCBInfo);

    m_device->updateDescriptorSets(writes);
}
