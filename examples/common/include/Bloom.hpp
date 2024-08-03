#pragma once

#include "FFT.hpp"

#include <ComputePipelins.hpp>
#include <glm/glm.hpp>

class Bloom : public ComputePipelines{
public:
    struct {
        int filterId{1};
        int n{2};
        float d0{0.1};
    } constants{};

    int blendMode{0};

    Bloom() = default;

    Bloom(glm::uvec2 imageSize);

    void init();

    void operator()(VkCommandBuffer commandBuffer, VulkanImage& image);

protected:
    std::vector<PipelineMetaData> pipelineMetaData() final;

    void copyFrom(VkCommandBuffer commandBuffer, VulkanImage& image);

    void fft(VkCommandBuffer commandBuffer);

    void updateMask(VkCommandBuffer commandBuffer);

    void convolute(VkCommandBuffer commandBuffer);

    void blend(VkCommandBuffer commandBuffer);

    void inverseFFT(VkCommandBuffer commandBuffer);

    void copyTo(VkCommandBuffer commandBuffer, VulkanImage& image);

    void addComputeBarrier(VkCommandBuffer commandBuffer, VulkanImage& image);

    void createTextures();

    void createDescriptorSets();

    void updateDescriptorSets();

    void clear(VkCommandBuffer commandBuffer);

private:
    FFT _fft;
    glm::uvec3 _imageSize{};
    glm::uvec3 _fftImageSize{};
    ComplexSignal _timeDomainSignal;
    ComplexSignal _inverseTimeDomainSignal;
    ComplexSignal _frequencyDomainSignal;
    ComplexSignal _maskSignal;
    VulkanDescriptorSetLayout _descriptorSetLayout;
    VkDescriptorSet _filterMaskDescriptorSet{};
    VkDescriptorSet _timeDomainDescriptorSet{};
    VkDescriptorSet _inverseTimeDomainDescriptorSet{};
    VkDescriptorSet _frequencyDomainDescriptorSet{};
};