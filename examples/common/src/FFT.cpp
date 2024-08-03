#include "FFT.hpp"

#include <utility/dft.hpp>

#include <complex>

FFT::FFT(size_t nSamples)
: ComputePipelines(&AppContext::device())
,_numPasses(std::ceil(std::log2(nSamples)))
, _numSamples(1 << to<int>(std::ceil(std::log2(nSamples))))
{}

FFT &FFT::init() {
    createButterflyLookup();
    createComputeTextures();
    createDescriptorSets();
    createPipelines();
    return *this;
}

void FFT::operator()(VkCommandBuffer commandBuffer, ComplexSignal &inSignal, ComplexSignal &outSignal) {
    _data.in = 0;
    _data.out = 1;
    _constants.inverse = 0;
    clear(commandBuffer, _data.signal[0].c);
    copy(commandBuffer, inSignal, _data.signal[0].c, {inSignal.real.width, inSignal.real.height});
    compute(commandBuffer, _data);
    copy(commandBuffer, _data.signal[_data.out].c, outSignal, { outSignal.real.width, outSignal.real.height});
}

void FFT::inverse(VkCommandBuffer commandBuffer, ComplexSignal &inSignal, ComplexSignal &outSignal) {
    _inverseData.in = 0;
    _inverseData.out = 1;
    _constants.inverse = 1;
    clear(commandBuffer, _inverseData.signal[0].c);
    copy(commandBuffer, inSignal, _inverseData.signal[0].c, {inSignal.real.width, inSignal.real.height});
    compute(commandBuffer, _inverseData);
    copy(commandBuffer, _inverseData.signal[_inverseData.out].c, outSignal, { outSignal.real.width, outSignal.real.height});
}

void FFT::createButterflyLookup() {
    const auto N = _numSamples;
    auto nButterflies = static_cast<int>(std::log2(N));
    std::vector<std::complex<double>> butterflyLut(N * nButterflies);
    std::vector<int> indexes(N * nButterflies * 2);
    createButterflyLookups(indexes, butterflyLut, nButterflies);

    std::vector<glm::vec2> butterflyLutVec2{};
    for(const auto& c : butterflyLut){
        butterflyLutVec2.emplace_back(c.real(), c.imag());
    }

    const auto& device = AppContext::device();
    textures::create(device, _data.butterfly.index, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SINT, indexes.data(), {N, nButterflies, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
    textures::create(device, _data.butterfly.lut, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SFLOAT, butterflyLutVec2.data(), {N, nButterflies, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);

    createButterflyLookups(indexes, butterflyLut, nButterflies, true);
    butterflyLutVec2.clear();
    for(const auto& c : butterflyLut){
        butterflyLutVec2.emplace_back(c.real(), c.imag());
    }

    textures::create(device, _inverseData.butterfly.index, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SINT, indexes.data(), {N, nButterflies, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
    textures::create(device, _inverseData.butterfly.lut, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SFLOAT, butterflyLutVec2.data(), {N, nButterflies, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
}

void FFT::createComputeTextures() {
    const auto N = _numSamples;
    const auto& device = AppContext::device();

    textures::create(device, _data.signal[0].c.real, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    textures::create(device, _data.signal[0].c.imaginary, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    textures::create(device, _data.signal[1].c.real, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    textures::create(device, _data.signal[1].c.imaginary, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    textures::create(device, _inverseData.signal[0].c.real, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    textures::create(device, _inverseData.signal[0].c.imaginary, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    textures::create(device, _inverseData.signal[1].c.real, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    textures::create(device, _inverseData.signal[1].c.imaginary, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    _data.signal[0].c.real.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    _data.signal[0].c.imaginary.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    _data.signal[1].c.real.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    _data.signal[1].c.imaginary.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    _inverseData.signal[0].c.real.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    _inverseData.signal[0].c.imaginary.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    _inverseData.signal[1].c.real.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    _inverseData.signal[1].c.imaginary.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
}

void FFT::compute(VkCommandBuffer commandBuffer, Data& data) {
    const auto N = _numSamples;
    const auto wc = workGroupCount;
    auto passes = _numPasses;
    const auto gc = glm::uvec3(N/wc.x, N/wc.y, 1);

    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = data.signal[data.in].descriptorSet;
    sets[1] = data.signal[data.out].descriptorSet;
    sets[2] = data.lookupDescriptorSet;

    // fft along x-axis
    _constants.N = to<int>(N);
    _constants.vertical = 0;
    for(auto pass = 0; pass < passes; ++pass){
        _constants.pass = pass;
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("fft"), 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("fft"));
        vkCmdPushConstants(commandBuffer, layout("fft"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(_constants), &_constants);
        vkCmdDispatch(commandBuffer, gc.x, gc.y, 1);

        AppContext::addImageMemoryBarriers(commandBuffer, { data.signal[data.out].c.real.image, data.signal[data.out].c.imaginary.image });
        std::swap(data.in, data.out);
        sets[0] = data.signal[data.in].descriptorSet;
        sets[1] = data.signal[data.out].descriptorSet;
    }

    // fft along y-axis
    _constants.vertical = 1;
    for(auto pass = 0; pass < passes; ++pass){
        _constants.pass = pass;
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("fft"), 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("fft"));
        vkCmdPushConstants(commandBuffer, layout("fft"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(_constants), &_constants);
        vkCmdDispatch(commandBuffer, gc.x, gc.y, 1);
        AppContext::addImageMemoryBarriers(commandBuffer, {  data.signal[data.out].c.real.image, data.signal[data.out].c.imaginary.image });
        if(pass < passes - 1){
            std::swap(data.in, data.out);
            sets[0] = data.signal[data.in].descriptorSet;
            sets[1] = data.signal[data.out].descriptorSet;
        }
    }
}

void FFT::createDescriptorSets() {
    auto& device = AppContext::device();

    _signalDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("fft_signal")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    _lookupDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("fft_butterfly_lookup")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    _srcSignalDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("fft_src_signal")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    auto sets = AppContext::descriptorPool().allocate({ _signalDescriptorSetLayout
                                                , _signalDescriptorSetLayout
                                                , _lookupDescriptorSetLayout
                                                , _signalDescriptorSetLayout
                                                , _signalDescriptorSetLayout
                                                , _lookupDescriptorSetLayout });

    _data.signal[0].descriptorSet = sets[0];
    _data.signal[1].descriptorSet = sets[1];
    _data.lookupDescriptorSet = sets[2];

    _inverseData.signal[0].descriptorSet = sets[3];
    _inverseData.signal[1].descriptorSet = sets[4];
    _inverseData.lookupDescriptorSet = sets[5];

    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("fft_signal_ping", _data.signal[0].descriptorSet);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("fft_signal_pong", _data.signal[1].descriptorSet);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("fft_lookup_table", _data.lookupDescriptorSet);


    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("inverse_fft_signal_ping", _inverseData.signal[0].descriptorSet);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("inverse_fft_signal_pong", _inverseData.signal[1].descriptorSet);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("inverse_fft_lookup_table", _inverseData.lookupDescriptorSet);

    auto writes = initializers::writeDescriptorSets<12>();

    writes[0].dstSet = _data.signal[0].descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo realImgInInfo{ VK_NULL_HANDLE, _data.signal[0].c.real.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[0].pImageInfo = &realImgInInfo;

    writes[1].dstSet = _data.signal[0].descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo imaginaryImgInInfo{ VK_NULL_HANDLE, _data.signal[0].c.imaginary.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[1].pImageInfo = &imaginaryImgInInfo;

    writes[2].dstSet = _data.signal[1].descriptorSet;
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo realImgOutInfo{ VK_NULL_HANDLE, _data.signal[1].c.real.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &realImgOutInfo;

    writes[3].dstSet = _data.signal[1].descriptorSet;
    writes[3].dstBinding = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo imaginaryImgOutInInfo{ VK_NULL_HANDLE, _data.signal[1].c.imaginary.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[3].pImageInfo = &imaginaryImgOutInInfo;

    writes[4].dstSet = _inverseData.signal[0].descriptorSet;
    writes[4].dstBinding = 0;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[4].descriptorCount = 1;
    VkDescriptorImageInfo inverse_realImgInInfo{ VK_NULL_HANDLE, _inverseData.signal[0].c.real.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[4].pImageInfo = &inverse_realImgInInfo;

    writes[5].dstSet = _inverseData.signal[0].descriptorSet;
    writes[5].dstBinding = 1;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[5].descriptorCount = 1;
    VkDescriptorImageInfo inverse_imaginaryImgInInfo{ VK_NULL_HANDLE, _inverseData.signal[0].c.imaginary.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[5].pImageInfo = &inverse_imaginaryImgInInfo;

    writes[6].dstSet = _inverseData.signal[1].descriptorSet;
    writes[6].dstBinding = 0;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[6].descriptorCount = 1;
    VkDescriptorImageInfo inverse_realImgOutInfo{ VK_NULL_HANDLE, _inverseData.signal[1].c.real.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[6].pImageInfo = &inverse_realImgOutInfo;

    writes[7].dstSet = _inverseData.signal[1].descriptorSet;
    writes[7].dstBinding = 1;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[7].descriptorCount = 1;
    VkDescriptorImageInfo inverse_imaginaryImgOutInInfo{ VK_NULL_HANDLE, _inverseData.signal[1].c.imaginary.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[7].pImageInfo = &inverse_imaginaryImgOutInInfo;

    writes[8].dstSet = _data.lookupDescriptorSet;
    writes[8].dstBinding = 0;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[8].descriptorCount = 1;
    VkDescriptorImageInfo lookupIndexInfo{ _data.butterfly.index.sampler.handle, _data.butterfly.index.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[8].pImageInfo = &lookupIndexInfo;

    writes[9].dstSet = _data.lookupDescriptorSet;
    writes[9].dstBinding = 1;
    writes[9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[9].descriptorCount = 1;
    VkDescriptorImageInfo butterflyLookupInfo{ _data.butterfly.lut.sampler.handle, _data.butterfly.lut.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[9].pImageInfo = &butterflyLookupInfo;

    writes[10].dstSet = _inverseData.lookupDescriptorSet;
    writes[10].dstBinding = 0;
    writes[10].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[10].descriptorCount = 1;
    VkDescriptorImageInfo inverseLookupIndexInfo{ _inverseData.butterfly.index.sampler.handle, _inverseData.butterfly.index.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[10].pImageInfo = &inverseLookupIndexInfo;

    writes[11].dstSet = _inverseData.lookupDescriptorSet;
    writes[11].dstBinding = 1;
    writes[11].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[11].descriptorCount = 1;
    VkDescriptorImageInfo inverseButterflyLookupInfo{ _inverseData.butterfly.lut.sampler.handle, _inverseData.butterfly.lut.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[11].pImageInfo = &inverseButterflyLookupInfo;

    device.updateDescriptorSets(writes);
}

std::vector<PipelineMetaData> FFT::pipelineMetaData() {
    return  {
            {"fft", AppContext::resource("fft_butterfly.comp.spv"),
             {&_signalDescriptorSetLayout, &_signalDescriptorSetLayout, &_lookupDescriptorSetLayout},
             {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(_constants)}}
            }
    };
}

void FFT::copy(VkCommandBuffer commandBuffer, ComplexSignal &src, ComplexSignal &dst, glm::uvec2 dim) {

    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.subresourceRange = DEFAULT_SUB_RANGE;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.image = src.real.image;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    barrier.image = src.imaginary.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = dst.real.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    barrier.image = dst.imaginary.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    VkImageCopy region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.mipLevel = 0;
    region.srcSubresource.baseArrayLayer = 0;
    region.srcSubresource.layerCount = 1;
    region.srcOffset = {0, 0};
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.mipLevel = 0;
    region.dstSubresource.baseArrayLayer = 0;
    region.dstSubresource.layerCount = 1;
    region.dstOffset = {0, 0};
    region.extent = { dim.x, dim.y, 1 };


    vkCmdCopyImage(commandBuffer, src.real.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.real.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    vkCmdCopyImage(commandBuffer, src.imaginary.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.imaginary.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = src.real.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    barrier.image = src.imaginary.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = dst.real.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    barrier.image = dst.imaginary.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,  0, 0, 0, 0, 0, 1, &barrier);
}

void FFT::clear(VkCommandBuffer commandBuffer, ComplexSignal &signal) {
    VkClearColorValue clearColor { 0.f, 0.f, 0.f, 0.f };
    vkCmdClearColorImage(commandBuffer, signal.real.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
    vkCmdClearColorImage(commandBuffer, signal.imaginary.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
}

VkImageMemoryBarrier FFT::createTransferDstToShaderReadBarrier(Texture& texture) {
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.subresourceRange = DEFAULT_SUB_RANGE;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = texture.image;

    return barrier;
}

VkImageMemoryBarrier FFT::createShaderReadToTransferDstBarrier(Texture& texture) {
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.subresourceRange = DEFAULT_SUB_RANGE;
    barrier.oldLayout = texture.image.currentLayout != VK_IMAGE_LAYOUT_UNDEFINED ? texture.image.currentLayout : VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = texture.image;

    return barrier;
}

void FFT::prepForCompute(VkCommandBuffer commandBuffer, ComplexSignal &cs) {
    auto barrier0 = createTransferDstToShaderReadBarrier(cs.real);
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, 0, 0, 0, 1, &barrier0);
    cs.real.image.currentLayout = VK_IMAGE_LAYOUT_GENERAL;

    auto barrier1 = createTransferDstToShaderReadBarrier(cs.imaginary);
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, 0, 0, 0, 1, &barrier1);
    cs.imaginary.image.currentLayout = VK_IMAGE_LAYOUT_GENERAL;
}

glm::uvec2 FFT::fftImageSize(glm::uvec2 imageSize) {
    auto size = std::max(imageSize.x, imageSize.y);
    auto sizeLog2 = to<int>(std::ceil(std::log2(size)));
    auto fftSize = 1 << sizeLog2;
    return glm::uvec2(fftSize);
}
