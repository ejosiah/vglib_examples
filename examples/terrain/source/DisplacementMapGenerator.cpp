#include "DisplacementMapGenerator.hpp"
#include "Barrier.hpp"

DisplacementMapGenerator::DisplacementMapGenerator(Context &context, DisplacementMethod method, uint width, uint height, std::string path)
    :m_context{&context},
     m_method{method},
     m_displacementMap{.width = width,.height = height },
     m_info{
        .values_tex_id = context.dmap_tex_index,
        .normal_tex_id = context.dmap_normal_tex_index,
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
        case DisplacementMethod::File:
            computeFileDisplacementMap(commandBuffer);
            break;
        case DisplacementMethod::FaultFormation:
            faultFormation(commandBuffer);
            break;
        default:
            assert(false && "method not not yet implemented!");
    }
    bindlessDescriptor().update({ &m_displacementMap.values, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_info.values_tex_id });

    generateNormalMap(commandBuffer);
    bindlessDescriptor().update({ &m_displacementMap.normals, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_info.normal_tex_id });
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
    VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    VkAccessFlagBits srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if( m_displacementMap.values.format != VK_FORMAT_R8G8B8A8_UNORM) {
        srcStageMask = VK_PIPELINE_STAGE_NONE;
        srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        srcAccessMask = VK_ACCESS_NONE;
        textures::createNoTransition(device(), m_displacementMap.values, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, {m_fileInfo.width, m_fileInfo.height, 1});
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

void DisplacementMapGenerator::faultFormation(VkCommandBuffer commandBuffer) {
    auto info = displacementMapInfo();
    auto& dispMap = m_displacementMap.values;

    VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    VkAccessFlagBits srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if(dispMap.format == VK_FORMAT_R16G16B16A16_SFLOAT || dispMap.width != info.width || dispMap.height != info.height) {
        srcStageMask = VK_PIPELINE_STAGE_NONE;
        srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        srcAccessMask = VK_ACCESS_NONE;
        textures::createNoTransition(device(), m_displacementMap.values, VK_IMAGE_TYPE_2D,
                                     VK_FORMAT_R16G16B16A16_SFLOAT, {info.width, info.height, 1});
    }

    static auto dispMapImageId = bindlessDescriptor().update(dispMap, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);


    Barriers::pushAndFlush(commandBuffer, dispMap.image, DEFAULT_SUB_RANGE, srcStageMask, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           srcAccessMask, VK_ACCESS_SHADER_WRITE_BIT, srcLayout, VK_IMAGE_LAYOUT_GENERAL);

    const auto gx = (info.width + 15)/16;
    const auto gy = (info.height + 15)/16;

    auto descriptorSet = bindlessDescriptorSet();
    const auto N = ff_constants.maxIterations;
    ff_constants.dmap_image_index = dispMapImageId;

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

    blur(commandBuffer);

}

std::vector<PipelineMetaData> DisplacementMapGenerator::metadata() {
    return {
            {
                .name = "generate_normals",
                .shadePath = FileManager::resource("generate_normal_map.comp.spv"),
                .layouts = { &bindlessDescriptorSetLayout() },
                .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NormalGenConstants)} }
            },
            {
                .name = "fault_formation",
                .shadePath = FileManager::resource("fault_formation.comp.spv"),
                .layouts = { &bindlessDescriptorSetLayout() },
                .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ff_constants)} }
            },
            {
                .name = "blur",
                .shadePath = FileManager::resource("blur.comp.spv"),
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
    if(m_method == DisplacementMethod::File) {
        rtVal.width = to<uint>(m_fileInfo.width);
        rtVal.height = to<uint>(m_fileInfo.height);
    }
    return rtVal;
}

void DisplacementMapGenerator::generateNormalMap(VkCommandBuffer commandBuffer) {
    auto info = displacementMapInfo();
    auto& normalMap = m_displacementMap.normals;

    VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    VkAccessFlagBits srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
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

    NormalGenConstants constants { 1000.f, 1.5f, 4,  info.values_tex_id, normalMapImageId } ;

    auto descriptorSet = bindlessDescriptorSet();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("generate_normals"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("generate_normals"), 0, 1, &descriptorSet, 0, 0);
    vkCmdPushConstants(commandBuffer, m_compute.layout("generate_normals"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, gx, gy, 1);

    Barriers::pushAndFlush(commandBuffer, normalMap.image, subresource, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    textures::generateLOD(commandBuffer, m_displacementMap.normals.image, info.width, info.height, levels);
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
        textures::createNoTransition(device(), blurOutput, VK_IMAGE_TYPE_2D, format, {info.width, info.height, 1});
        textures::createNoTransition(device(), blurInput, VK_IMAGE_TYPE_2D, format, {info.width, info.height, 1});

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


