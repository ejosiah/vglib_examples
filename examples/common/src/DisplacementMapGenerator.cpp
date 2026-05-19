#include "vista/DisplacementMapGenerator.hpp"
#include "Barrier.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>

namespace {
    constexpr VkSamplerAddressMode DepthMapAddressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    constexpr VkPipelineStageFlags2 GeneratedTextureReadStages =
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

    constexpr VkAccessFlags2 GeneratedTextureReadAccess = VK_ACCESS_2_SHADER_READ_BIT;

    bool sliderUint(const char* label, uint& value, int min, int max) {
        int current = static_cast<int>(value);
        if(!ImGui::SliderInt(label, &current, min, max)) {
            return false;
        }

        value = static_cast<uint>(std::clamp(current, min, max));
        return true;
    }
}

DisplacementMapGenerator::DisplacementMapGenerator(Context &context, DisplacementMethod method, uint width, uint height, std::string path)
    :m_context{&context},
     m_method{method},
     m_displacementMap{.width = width,.height = height },
     m_info{
        .values_tex_id = context.dmap_tex_index,
        .normal_tex_id = context.dmap_normal_tex_index,
        .slope_moments0_tex_id = context.dmap_slope_moments0_tex_index,
        .slope_moments1_tex_id = context.dmap_slope_moments1_tex_index,
        .width = width,
        .height = height
    },
    m_path{path}
    {}

void DisplacementMapGenerator::init() {
    createComputePipelines();
    loadDisplacementMap();
    device().graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
       exec(commandBuffer);
    });
}

void DisplacementMapGenerator::exec(VkCommandBuffer commandBuffer) {
    switch(m_method){
        case DisplacementMethod::None:
            noneDisplacementMap(commandBuffer);
            break;
        case DisplacementMethod::File:
            computeFileDisplacementMap(commandBuffer);
            break;
        case DisplacementMethod::FaultFormation:
            faultFormation(commandBuffer);
            break;
        case DisplacementMethod::Noise:
            noiseHeightMap(commandBuffer);
            break;
        default:
            assert(false && "method not not yet implemented!");
    }
    bindlessDescriptor().update({ &m_displacementMap.values, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_info.values_tex_id });

    refreshDerivedMaps(commandBuffer);
}

Texture& DisplacementMapGenerator::displacementTexture() {
    return m_displacementMap.values;
}

void DisplacementMapGenerator::refreshDerivedMaps(VkCommandBuffer commandBuffer) {
    generateSlopeMomentMaps(commandBuffer);
    bindlessDescriptor().update({ &m_displacementMap.slopeMoments0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_info.slope_moments0_tex_id });
    bindlessDescriptor().update({ &m_displacementMap.slopeMoments1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_info.slope_moments1_tex_id });

    generateNormalMap(commandBuffer);
    bindlessDescriptor().update({ &m_displacementMap.normals, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_info.normal_tex_id });
}

bool DisplacementMapGenerator::regenerateIfNeeded(VkCommandBuffer commandBuffer) {
    if(!m_dirty) {
        return false;
    }

    m_dirty = false;
    if(m_method == DisplacementMethod::File) {
        return false;
    }

    exec(commandBuffer);
    return true;
}

bool DisplacementMapGenerator::controls(bool show) {
    if(!show) {
        return false;
    }

    bool dirty = false;

    ImGui::Begin("Displacement");
    ImGui::SetWindowSize({0, 0});

    static constexpr std::array<const char*, 4> methods{ "None", "File", "Fault formation", "Noise" };
    int method = static_cast<int>(m_method);
    if(ImGui::Combo("Type", &method, methods.data(), static_cast<int>(methods.size()))) {
        m_method = static_cast<DisplacementMethod>(method);
        dirty = true;
    }

    if(m_method == DisplacementMethod::File || m_method == DisplacementMethod::None) {
        auto info = displacementMapInfo();
        ImGui::Text("%s: %u x %u", m_method == DisplacementMethod::File ? "File" : "Input", info.width, info.height);
    }else {
        int size[2] = { static_cast<int>(m_info.width), static_cast<int>(m_info.height) };
        if(ImGui::InputInt2("Size", size)) {
            m_info.width = static_cast<uint>(std::clamp(size[0], 16, 8192));
            m_info.height = static_cast<uint>(std::clamp(size[1], 16, 8192));
            m_displacementMap.width = m_info.width;
            m_displacementMap.height = m_info.height;
            dirty = true;
        }
    }

    if(m_method == DisplacementMethod::FaultFormation) {
        dirty |= ImGui::DragFloat2("Seed", &ff_options.seed.x, 1.0f);
        dirty |= sliderUint("Iterations", ff_options.maxIterations, 1, 10000);
        dirty |= ImGui::Checkbox("Blur", &ff_options.blur);
        if(ff_options.blur) {
            dirty |= ImGui::SliderInt("Blur iterations", &ff_options.blurIterations, 1, 64);
        }
    }else if(m_method == DisplacementMethod::Noise) {
        dirty |= ImGui::DragFloat2("Seed", &noise_constants.seed.x, 1.0f);
        dirty |= ImGui::DragFloat("Base frequency", &noise_constants.baseFrequency, 0.05f, 0.001f, 64.0f, "%.3f");
        dirty |= ImGui::DragFloat("Lacunarity", &noise_constants.lacunarity, 0.01f, 1.001f, 8.0f, "%.3f");
        dirty |= ImGui::SliderFloat("Gain", &noise_constants.gain, 0.0f, 1.0f);
        dirty |= sliderUint("Octaves", noise_constants.octaves, 1, 12);
        bool enableRidges = noise_constants.enableRidges == 1;
        if(ImGui::Checkbox("Ridges", &enableRidges)) {
            noise_constants.enableRidges = enableRidges ? 1u : 0u;
            dirty = true;
        }
    }

    if(m_method != DisplacementMethod::File && ImGui::Button("Regenerate")) {
        dirty = true;
    }

    ImGui::End();

    m_dirty |= dirty;
    return dirty;
}

void DisplacementMapGenerator::loadDisplacementMap() {
    if(m_path.empty()) return;

    stbi_set_flip_vertically_on_load(0);
    auto pixels = stbi_load(m_path.c_str(), &m_fileInfo.width, &m_fileInfo.height, &m_fileInfo.channels, STBI_rgb_alpha);
    if(!pixels){
        throw std::runtime_error{fmt::format("failed to load texture image {}!", m_path)};
    }
    VkDeviceSize size = m_fileInfo.width * m_fileInfo.height * STBI_rgb_alpha;
    m_fileInfo.pixels = device().createDeviceLocalBuffer(pixels, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    stbi_image_free(pixels);
}

void DisplacementMapGenerator::createComputePipelines() {
    m_compute = ComputePipelines(&device(), metadata());
    m_compute.createPipelines();
}

void DisplacementMapGenerator::computeFileDisplacementMap(VkCommandBuffer commandBuffer) {
    VkPipelineStageFlags2 srcStageMask = GeneratedTextureReadStages;
    VkAccessFlags2 srcAccessMask = GeneratedTextureReadAccess;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if( m_displacementMap.values.format != VK_FORMAT_R8G8B8A8_UNORM) {
        srcStageMask = VK_PIPELINE_STAGE_NONE;
        srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        srcAccessMask = VK_ACCESS_NONE;
        textures::createNoTransition(device(), m_displacementMap.values, VK_IMAGE_TYPE_2D,
                                     VK_FORMAT_R8G8B8A8_UNORM, {m_fileInfo.width, m_fileInfo.height, 1},
                                     DepthMapAddressMode);
    }

    VkBufferImageCopy2 region{ VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2 };
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { to<uint>(m_fileInfo.width), to<uint>(m_fileInfo.height), 1 };

    VkCopyBufferToImageInfo2 copyInfo{ VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2 };
    copyInfo.srcBuffer = m_fileInfo.pixels;
    copyInfo.dstImage = m_displacementMap.values.image;
    copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    copyInfo.regionCount = 1;
    copyInfo.pRegions = &region;

    Barriers::pushAndFlush(commandBuffer, m_displacementMap.values.image, DEFAULT_SUB_RANGE, srcStageMask,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, srcAccessMask, VK_ACCESS_TRANSFER_WRITE_BIT,
                           srcLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    vkCmdCopyBufferToImage2(commandBuffer, &copyInfo);

    Barriers::pushAndFlush(commandBuffer, m_displacementMap.values.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

}

void DisplacementMapGenerator::noneDisplacementMap(VkCommandBuffer commandBuffer) {
    auto info = displacementMapInfo();
    auto& dispMap = m_displacementMap.values;

    VkPipelineStageFlags2 srcStageMask = GeneratedTextureReadStages;
    VkAccessFlags2 srcAccessMask = GeneratedTextureReadAccess;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if(dispMap.format != VK_FORMAT_R16_SFLOAT || dispMap.width != info.width || dispMap.height != info.height) {
        srcStageMask = VK_PIPELINE_STAGE_NONE;
        srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        srcAccessMask = VK_ACCESS_NONE;
        textures::createNoTransition(device(), dispMap, VK_IMAGE_TYPE_2D,
                                     VK_FORMAT_R16_SFLOAT, {info.width, info.height, 1},
                                     DepthMapAddressMode);
    }

    Barriers::pushAndFlush(commandBuffer, dispMap.image, DEFAULT_SUB_RANGE, srcStageMask, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           srcAccessMask, VK_ACCESS_TRANSFER_WRITE_BIT, srcLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkClearColorValue clearColor{};
    vkCmdClearColorImage(commandBuffer, dispMap.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &DEFAULT_SUB_RANGE);

    Barriers::pushAndFlush(commandBuffer, dispMap.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    dispMap.image.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void DisplacementMapGenerator::faultFormation(VkCommandBuffer commandBuffer) {
    auto info = displacementMapInfo();
    auto& dispMap = m_displacementMap.values;

    textures::createNoTransition(device(), m_displacementMap.values, VK_IMAGE_TYPE_2D,
                                 VK_FORMAT_R16_SFLOAT, {info.width, info.height, 1},
                                 DepthMapAddressMode);

    if(m_faultFormationImageId == ~0u) {
        m_faultFormationImageId = bindlessDescriptor().reserveImageSlots(1);
    }
    bindlessDescriptor().update({ &dispMap, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_faultFormationImageId, VK_IMAGE_LAYOUT_GENERAL });

    Barriers::pushAndFlush(commandBuffer, dispMap.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    const auto gx = (info.width + 15)/16;
    const auto gy = (info.height + 15)/16;

    auto descriptorSet = bindlessDescriptorSet();
    ff_constants.seed = ff_options.seed;
    ff_constants.maxIterations = ff_options.maxIterations;
    const auto N = ff_constants.maxIterations;
    ff_constants.dmap_image_index = m_faultFormationImageId;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("fault_formation"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("fault_formation"), 0, 1, &descriptorSet, 0, 0);

    for(int i = 0; i <= N; ++i) {
        ff_constants.iteration = i;
        vkCmdPushConstants(commandBuffer, m_compute.layout("fault_formation"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ff_constants), &ff_constants);
        vkCmdDispatch(commandBuffer, gx, gy, 1);

        Barrier::computeWriteToRead(commandBuffer);
    }

    Barriers::pushAndFlush(commandBuffer, dispMap.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    dispMap.image.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if(ff_options.blur) {
        blur(commandBuffer);
    }

}

void DisplacementMapGenerator::noiseHeightMap(VkCommandBuffer commandBuffer) {
    auto info = displacementMapInfo();
    auto& dispMap = m_displacementMap.values;

    VkPipelineStageFlags2 srcStageMask = GeneratedTextureReadStages;
    VkAccessFlags2 srcAccessMask = GeneratedTextureReadAccess;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if(dispMap.format != VK_FORMAT_R16_SFLOAT || dispMap.width != info.width || dispMap.height != info.height) {
        srcStageMask = VK_PIPELINE_STAGE_NONE;
        srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        srcAccessMask = VK_ACCESS_NONE;
        textures::createNoTransition(device(), dispMap, VK_IMAGE_TYPE_2D,
                                     VK_FORMAT_R16_SFLOAT, {info.width, info.height, 1},
                                     DepthMapAddressMode);
    }

    if(m_noiseImageId == ~0u) {
        m_noiseImageId = bindlessDescriptor().reserveImageSlots(1);
    }

    bindlessDescriptor().update({ &dispMap, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_noiseImageId, VK_IMAGE_LAYOUT_GENERAL });

    Barriers::pushAndFlush(commandBuffer, dispMap.image, DEFAULT_SUB_RANGE, srcStageMask, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           srcAccessMask, VK_ACCESS_SHADER_WRITE_BIT, srcLayout, VK_IMAGE_LAYOUT_GENERAL);

    noise_constants.dmap_image_index = m_noiseImageId;
    const auto gx = (info.width + 31)/32;
    const auto gy = (info.height + 31)/32;

    auto descriptorSet = bindlessDescriptorSet();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("noise_height_map_gen"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("noise_height_map_gen"), 0, 1, &descriptorSet, 0, 0);
    vkCmdPushConstants(commandBuffer, m_compute.layout("noise_height_map_gen"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(noise_constants), &noise_constants);
    vkCmdDispatch(commandBuffer, gx, gy, 1);

    Barriers::pushAndFlush(commandBuffer, dispMap.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                           VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    dispMap.image.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

std::vector<PipelineMetaData> DisplacementMapGenerator::metadata() {
    return {
            {
                .name = "generate_normals",
                .shadePath = FileManager::resource("vista_generate_normal_map.comp.spv"),
                .layouts = { &bindlessDescriptorSetLayout() },
                .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NormalGenConstants)} }
            },
            {
                .name = "generate_slope_moments",
                .shadePath = FileManager::resource("vista_slope_moments.comp.spv"),
                .layouts = { &bindlessDescriptorSetLayout() },
                .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SlopeMomentConstants)} }
            },
            {
                .name = "fault_formation",
                .shadePath = FileManager::resource("vista_fault_formation.comp.spv"),
                .layouts = { &bindlessDescriptorSetLayout() },
                .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ff_constants)} }
            },
            {
                .name = "noise_height_map_gen",
                .shadePath = FileManager::resource("vista_noise_height_map_gen.comp.spv"),
                .layouts = { &bindlessDescriptorSetLayout() },
                .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NoiseConstants)} }
            },
            {
                .name = "blur",
                .shadePath = FileManager::resource("vista_blur.comp.spv"),
                .layouts = { &bindlessDescriptorSetLayout() },
                .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint) * 3} }
            },
    };
}

VulkanDevice &DisplacementMapGenerator::device() {
    return *m_context->device;
}

DisplacementMapInfo DisplacementMapGenerator::displacementMapInfo() const {
    auto rtVal = m_info;
    if((m_method == DisplacementMethod::File || m_method == DisplacementMethod::None) && m_fileInfo.width > 0 && m_fileInfo.height > 0) {
        rtVal.width = to<uint>(m_fileInfo.width);
        rtVal.height = to<uint>(m_fileInfo.height);
    }
    return rtVal;
}

void DisplacementMapGenerator::setTerrainMetrics(glm::vec2 terrainWorldSize, glm::vec2 heightScale) {
    const float heightRange = std::abs(heightScale.y - heightScale.x);
    m_derivedMapHeightScale = {
        heightRange / std::max(std::abs(terrainWorldSize.x), 0.000001f),
        heightRange / std::max(std::abs(terrainWorldSize.y), 0.000001f)
    };
}

void DisplacementMapGenerator::generateNormalMap(VkCommandBuffer commandBuffer) {
    auto info = displacementMapInfo();
    auto& normalMap = m_displacementMap.normals;

    VkPipelineStageFlags2 srcStageMask = GeneratedTextureReadStages;
    VkAccessFlags2 srcAccessMask = GeneratedTextureReadAccess;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    const auto levels = to<uint>(std::log2(std::max(info.width, info.height))) + 1u;
    if(normalMap.width != info.width || normalMap.height != info.height) {
        srcStageMask = VK_PIPELINE_STAGE_NONE;
        srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        srcAccessMask = VK_ACCESS_NONE;
        m_displacementMap.normals.levels = levels;
        textures::createNoTransition(device(), m_displacementMap.normals, VK_IMAGE_TYPE_2D,
                                     VK_FORMAT_R16G16B16A16_SFLOAT, {info.width, info.height, 1});
    }

    static auto normalMapImageId = bindlessDescriptor().update(normalMap, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);


    auto subresource = DEFAULT_SUB_RANGE;
    subresource.levelCount = levels;
    Barriers::pushAndFlush(commandBuffer, normalMap.image, subresource, srcStageMask, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           srcAccessMask, VK_ACCESS_SHADER_WRITE_BIT, srcLayout, VK_IMAGE_LAYOUT_GENERAL);

    const auto gx = (info.width + 15)/16;
    const auto gy = (info.height + 15)/16;

    NormalGenConstants constants {
        .bump_strength = 1000.0f,
        .sigma = 1.5f,
        .sampleRadius = 4,
        .heightScaleX = m_derivedMapHeightScale.x,
        .heightScaleY = m_derivedMapHeightScale.y,
        .dmap_tex_id = info.values_tex_id,
        .normal_image_id = normalMapImageId
    };

    auto descriptorSet = bindlessDescriptorSet();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("generate_normals"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("generate_normals"), 0, 1, &descriptorSet, 0, 0);
    vkCmdPushConstants(commandBuffer, m_compute.layout("generate_normals"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, gx, gy, 1);

    Barriers::pushAndFlush(commandBuffer, normalMap.image, subresource, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    textures::generateLOD(commandBuffer, m_displacementMap.normals.image, info.width, info.height, levels);
}

void DisplacementMapGenerator::generateSlopeMomentMaps(VkCommandBuffer commandBuffer) {
    auto info = displacementMapInfo();
    auto& moments0 = m_displacementMap.slopeMoments0;
    auto& moments1 = m_displacementMap.slopeMoments1;

    VkPipelineStageFlags2 srcStageMask = GeneratedTextureReadStages;
    VkAccessFlags2 srcAccessMask = GeneratedTextureReadAccess;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    const auto levels = to<uint>(std::log2(std::max(info.width, info.height))) + 1u;
    const bool recreate = moments0.width != info.width || moments0.height != info.height
        || moments1.width != info.width || moments1.height != info.height;
    if(recreate) {
        srcStageMask = VK_PIPELINE_STAGE_NONE;
        srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        srcAccessMask = VK_ACCESS_NONE;
        moments0.levels = levels;
        moments1.levels = levels;
        textures::createNoTransition(device(), moments0, VK_IMAGE_TYPE_2D,
                                     VK_FORMAT_R32G32B32A32_SFLOAT, {info.width, info.height, 1});
        textures::createNoTransition(device(), moments1, VK_IMAGE_TYPE_2D,
                                     VK_FORMAT_R32G32B32A32_SFLOAT, {info.width, info.height, 1});
    }

    if(m_slopeMoments0ImageId == ~0u) {
        m_slopeMoments0ImageId = bindlessDescriptor().reserveImageSlots(1);
    }
    if(m_slopeMoments1ImageId == ~0u) {
        m_slopeMoments1ImageId = bindlessDescriptor().reserveImageSlots(1);
    }

    bindlessDescriptor().update({ &moments0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_slopeMoments0ImageId, VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &moments1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_slopeMoments1ImageId, VK_IMAGE_LAYOUT_GENERAL });

    auto subresource = DEFAULT_SUB_RANGE;
    subresource.levelCount = levels;
    Barriers::push(moments0.image, subresource, srcStageMask, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   srcAccessMask, VK_ACCESS_SHADER_WRITE_BIT, srcLayout, VK_IMAGE_LAYOUT_GENERAL);
    Barriers::push(moments1.image, subresource, srcStageMask, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   srcAccessMask, VK_ACCESS_SHADER_WRITE_BIT, srcLayout, VK_IMAGE_LAYOUT_GENERAL);
    Barriers::flush(commandBuffer);

    const auto gx = (info.width + 15)/16;
    const auto gy = (info.height + 15)/16;

    SlopeMomentConstants constants {
        .heightScaleX = m_derivedMapHeightScale.x,
        .heightScaleY = m_derivedMapHeightScale.y,
        .dmap_tex_id = m_info.values_tex_id,
        .moments0_image_id = m_slopeMoments0ImageId,
        .moments1_image_id = m_slopeMoments1ImageId
    };

    auto descriptorSet = bindlessDescriptorSet();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("generate_slope_moments"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("generate_slope_moments"), 0, 1, &descriptorSet, 0, 0);
    vkCmdPushConstants(commandBuffer, m_compute.layout("generate_slope_moments"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, gx, gy, 1);

    Barriers::push(moments0.image, subresource, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                   VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Barriers::push(moments1.image, subresource, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                   VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Barriers::flush(commandBuffer);

    textures::generateLOD(commandBuffer, moments0.image, info.width, info.height, levels);
    textures::generateLOD(commandBuffer, moments1.image, info.width, info.height, levels);
}


VulkanDescriptorSetLayout &DisplacementMapGenerator::bindlessDescriptorSetLayout() {
    return const_cast<VulkanDescriptorSetLayout &>(*m_context->bindlessDescriptor->descriptorSetLayout);
}

VkDescriptorSet DisplacementMapGenerator::bindlessDescriptorSet() {
    return m_context->bindlessDescriptor->descriptorSet;
}

BindlessDescriptor &DisplacementMapGenerator::bindlessDescriptor() {
    return *m_context->bindlessDescriptor;
}

void DisplacementMapGenerator::blur(VkCommandBuffer commandBuffer) {
    auto info = displacementMapInfo();
    static struct {
        uint horizontal;
        uint blur_input_index;
        uint blur_output_index;
    } constants {0 ,0, 0};

    static Texture blurInput{};
    static Texture blurOutput{};


    if(blurOutput.format == VK_FORMAT_UNDEFINED || blurOutput.width != info.width || blurOutput.height != info.height) {
        auto format = m_displacementMap.values.format;
        textures::createNoTransition(device(), blurOutput, VK_IMAGE_TYPE_2D, format, {info.width, info.height, 1},
                                     DepthMapAddressMode);
        textures::createNoTransition(device(), blurInput, VK_IMAGE_TYPE_2D, format, {info.width, info.height, 1},
                                     DepthMapAddressMode);

        Barriers::push(blurInput.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

        Barriers::push(blurOutput.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

        Barriers::flush(commandBuffer);
        blurInput.image.currentLayout = VK_IMAGE_LAYOUT_GENERAL;
        blurOutput.image.currentLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    textures::copy(commandBuffer, m_displacementMap.values, blurInput);

    static auto blur_input_offset = to<uint>(bindlessDescriptor().reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2));
    static auto blur_output_offset = to<uint>(bindlessDescriptor().reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2));

    static std::array<uint, 2> blur_input_index{blur_input_offset, blur_input_offset+1};
    static std::array<uint, 2> blur_output_index{blur_output_offset, blur_output_offset+1};

    bindlessDescriptor().update({ &blurInput, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, blur_input_index[0], VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &blurOutput, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, blur_input_index[1], VK_IMAGE_LAYOUT_GENERAL });

    bindlessDescriptor().update({ &blurOutput, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, blur_output_index[0], VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &blurInput, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, blur_output_index[1], VK_IMAGE_LAYOUT_GENERAL });


    int pingPong = 0;
    const auto iterations = ff_options.blurIterations;  // use odd number iterations so blurOut will always be final output
    const auto gx = (info.width + 15)/16;
    const auto gy = (info.height + 15)/16;
    auto descriptorSet = bindlessDescriptorSet();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("blur"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("blur"), 0, 1, &descriptorSet, 0, 0);

    for(auto i = 0; i < iterations; ++i) {
        constants.horizontal = 1;
        constants.blur_input_index = blur_input_index[pingPong];
        constants.blur_output_index = blur_output_index[pingPong];

        vkCmdPushConstants(commandBuffer, m_compute.layout("blur"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
        vkCmdDispatch(commandBuffer, gx, gy, 1);
        Barrier::computeWriteToRead(commandBuffer);

        constants.horizontal = 0;
        vkCmdPushConstants(commandBuffer, m_compute.layout("blur"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
        vkCmdDispatch(commandBuffer, gx, gy, 1);
        Barrier::computeWriteToRead(commandBuffer);

        pingPong = 1 - pingPong;
    }

    textures::copy(commandBuffer, blurOutput, m_displacementMap.values);

}
