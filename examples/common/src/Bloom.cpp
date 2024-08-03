#include "Bloom.hpp"
#include "AppContext.hpp"

Bloom::Bloom(glm::uvec2 imageSize)
: ComputePipelines(&AppContext::device())
, _imageSize(imageSize, 1)
, _fftImageSize(FFT::fftImageSize(imageSize), 1)
{}

void Bloom::init() {
    auto size = std::max(_imageSize.x, _imageSize.y);
    _fft = FFT{size};
    _fft.init();
    createTextures();
    createDescriptorSets();
    updateDescriptorSets();
    createPipelines();
}

void Bloom::operator()(VkCommandBuffer commandBuffer, VulkanImage& image) {
    clear(commandBuffer);
    copyFrom(commandBuffer, image);
    updateMask(commandBuffer);
    fft(commandBuffer);
    convolute(commandBuffer);
    inverseFFT(commandBuffer);
    blend(commandBuffer);
    copyTo(commandBuffer, image);
}

std::vector<PipelineMetaData> Bloom::pipelineMetaData() {
    return {
            {
                "filter_mask",
                AppContext::resource("mask.comp.spv"),
                {  &_descriptorSetLayout  },
                { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants) } }
            },
            {
                "convolute",
                AppContext::resource("apply_mask.comp.spv"),
                { &_descriptorSetLayout, &_descriptorSetLayout, &_descriptorSetLayout }
            },
            {
                "blend",
                AppContext::resource("blend.comp.spv"),
                { &_descriptorSetLayout, &_descriptorSetLayout, &_descriptorSetLayout },
                { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int) } }
            }
    };
}

void Bloom::createDescriptorSets() {
    _descriptorSetLayout =
        AppContext::device().descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();
}

void Bloom::updateDescriptorSets() {
    auto sets = AppContext::descriptorPool().allocate( {
        _descriptorSetLayout,_descriptorSetLayout, _descriptorSetLayout, _descriptorSetLayout });
    _filterMaskDescriptorSet = sets[0];
    _timeDomainDescriptorSet = sets[1];
    _frequencyDomainDescriptorSet = sets[2];
    _inverseTimeDomainDescriptorSet = sets[3];

    auto writes = initializers::writeDescriptorSets<8>();
    std::vector<VkDescriptorImageInfo> info(8);

    info[0] = { VK_NULL_HANDLE, _maskSignal.real.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    info[1] = { VK_NULL_HANDLE, _timeDomainSignal.real.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    info[2] = { VK_NULL_HANDLE, _frequencyDomainSignal.real.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    info[3] = { VK_NULL_HANDLE, _inverseTimeDomainSignal.real.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };

    info[4] = { VK_NULL_HANDLE, _maskSignal.imaginary.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    info[5] = { VK_NULL_HANDLE, _timeDomainSignal.imaginary.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    info[6] = { VK_NULL_HANDLE, _frequencyDomainSignal.imaginary.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    info[7] = { VK_NULL_HANDLE, _inverseTimeDomainSignal.imaginary.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };


    writes[0].dstSet = _filterMaskDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &info[0];

    writes[1].dstSet = _timeDomainDescriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &info[1];

    writes[2].dstSet = _frequencyDomainDescriptorSet;
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &info[2];

    writes[3].dstSet = _inverseTimeDomainDescriptorSet;
    writes[3].dstBinding = 0;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].descriptorCount = 1;
    writes[3].pImageInfo = &info[3];

    writes[4].dstSet = _filterMaskDescriptorSet;
    writes[4].dstBinding = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[4].descriptorCount = 1;
    writes[4].pImageInfo = &info[4];

    writes[5].dstSet = _timeDomainDescriptorSet;
    writes[5].dstBinding = 1;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[5].descriptorCount = 1;
    writes[5].pImageInfo = &info[5];

    writes[6].dstSet = _frequencyDomainDescriptorSet;
    writes[6].dstBinding = 1;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[6].descriptorCount = 1;
    writes[6].pImageInfo = &info[6];

    writes[7].dstSet = _inverseTimeDomainDescriptorSet;
    writes[7].dstBinding = 1;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[7].descriptorCount = 1;
    writes[7].pImageInfo = &info[7];

    AppContext::device().updateDescriptorSets(writes);
}

void Bloom::createTextures() {
    auto& device = AppContext::device();
    textures::createNoTransition(device, _timeDomainSignal.real, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, _imageSize);
    textures::createNoTransition(device, _timeDomainSignal.imaginary, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, _imageSize);

    textures::createNoTransition(device, _maskSignal.real, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, _fftImageSize);
    textures::createNoTransition(device, _maskSignal.imaginary, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, _fftImageSize);
    textures::createNoTransition(device, _frequencyDomainSignal.real, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, _fftImageSize);
    textures::createNoTransition(device, _frequencyDomainSignal.imaginary, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, _fftImageSize);
    textures::createNoTransition(device, _inverseTimeDomainSignal.real, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, _fftImageSize);
    textures::createNoTransition(device, _inverseTimeDomainSignal.imaginary, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, _fftImageSize);

    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_NONE;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.subresourceRange = DEFAULT_SUB_RANGE;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){

        barrier.image = _timeDomainSignal.real.image;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

        barrier.image = _timeDomainSignal.imaginary.image;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

        barrier.image = _maskSignal.real.image;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

        barrier.image = _maskSignal.imaginary.image;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);


        barrier.image = _frequencyDomainSignal.real.image;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

        barrier.image = _frequencyDomainSignal.imaginary.image;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

        barrier.image = _inverseTimeDomainSignal.real.image;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

        barrier.image = _inverseTimeDomainSignal.imaginary.image;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    });

}

void Bloom::copyFrom(VkCommandBuffer commandBuffer, VulkanImage &image) {
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.subresourceRange = DEFAULT_SUB_RANGE;
    barrier.oldLayout = image.currentLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.image = image.image;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.image = _timeDomainSignal.real.image;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    auto region = initializers::imageCopy(_imageSize);
    vkCmdCopyImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _timeDomainSignal.real.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);


    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = _timeDomainSignal.real.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);
}

void Bloom::copyTo(VkCommandBuffer commandBuffer, VulkanImage &image) {
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.subresourceRange = DEFAULT_SUB_RANGE;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.image = _timeDomainSignal.real.image;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    auto region = initializers::imageCopy(_imageSize);
    vkCmdCopyImage(commandBuffer, _timeDomainSignal.real.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = _timeDomainSignal.real.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = image.currentLayout;
    barrier.image = image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

}

void Bloom::clear(VkCommandBuffer commandBuffer) {
    VkClearColorValue clearColor { 0.f, 0.f, 0.f, 0.f };
    vkCmdClearColorImage(commandBuffer, _timeDomainSignal.real.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
    vkCmdClearColorImage(commandBuffer, _timeDomainSignal.imaginary.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
    vkCmdClearColorImage(commandBuffer, _maskSignal.real.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
    vkCmdClearColorImage(commandBuffer, _maskSignal.imaginary.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
    vkCmdClearColorImage(commandBuffer, _frequencyDomainSignal.real.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
    vkCmdClearColorImage(commandBuffer, _frequencyDomainSignal.imaginary.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
    vkCmdClearColorImage(commandBuffer, _inverseTimeDomainSignal.real.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
    vkCmdClearColorImage(commandBuffer, _inverseTimeDomainSignal.imaginary.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
}

void Bloom::fft(VkCommandBuffer commandBuffer) {
    _fft(commandBuffer, _timeDomainSignal, _frequencyDomainSignal);
    addComputeBarrier(commandBuffer, _frequencyDomainSignal.real.image);
    addComputeBarrier(commandBuffer, _frequencyDomainSignal.imaginary.image);
}

void Bloom::updateMask(VkCommandBuffer commandBuffer) {
    const auto gc = _fftImageSize.xy()/32u;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("filter_mask"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("filter_mask"),  0, 1, &_filterMaskDescriptorSet, 0, 0);
    vkCmdPushConstants(commandBuffer, layout("filter_mask"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, gc.x, gc.x, 1);
    addComputeBarrier(commandBuffer, _maskSignal.real.image);
}

void Bloom::addComputeBarrier(VkCommandBuffer commandBuffer, VulkanImage &image) {
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.subresourceRange = DEFAULT_SUB_RANGE;
    barrier.image = image.image;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);
}

void Bloom::blend(VkCommandBuffer commandBuffer) {
    const auto gc = _fftImageSize.xy()/32u;

    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = _timeDomainDescriptorSet;
    sets[1] = _inverseTimeDomainDescriptorSet;
    sets[2] = _timeDomainDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("blend"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("blend"),  0, sets.size(), sets.data(), 0, 0);
    vkCmdPushConstants(commandBuffer, layout("blend"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &blendMode);
    vkCmdDispatch(commandBuffer, gc.x, gc.x, 1);
    addComputeBarrier(commandBuffer, _inverseTimeDomainSignal.real.image);
    addComputeBarrier(commandBuffer, _inverseTimeDomainSignal.imaginary.image);
}

void Bloom::convolute(VkCommandBuffer commandBuffer) {
    const auto gc = _fftImageSize.xy()/32u;

    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = _frequencyDomainDescriptorSet;
    sets[1] = _filterMaskDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("convolute"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("convolute"),  0, sets.size(), sets.data(), 0, 0);
    vkCmdDispatch(commandBuffer, gc.x, gc.x, 1);
    addComputeBarrier(commandBuffer, _frequencyDomainSignal.real.image);
    addComputeBarrier(commandBuffer, _frequencyDomainSignal.imaginary.image);
}

void Bloom::inverseFFT(VkCommandBuffer commandBuffer) {
    _fft.inverse(commandBuffer, _frequencyDomainSignal, _inverseTimeDomainSignal);
    addComputeBarrier(commandBuffer, _inverseTimeDomainSignal.real.image);
    addComputeBarrier(commandBuffer, _inverseTimeDomainSignal.imaginary.image);
}

