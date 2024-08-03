#pragma once

#include "AppContext.hpp"
#include <ComputePipelins.hpp>
#include <Texture.h>

#include <array>

struct ComplexSignal {
    Texture real;
    Texture imaginary;
};

class FFT : public ComputePipelines {
public:
    struct Data{
        struct {
            VkDescriptorSet descriptorSet{};
            ComplexSignal c;
        } signal[2];

        VkDescriptorSet lookupDescriptorSet{};

        struct {
            Texture index;
            Texture lut;
        } butterfly;

        int in{0};
        int out{1};
    };

    FFT() = default;

    explicit FFT(size_t nSamples);

    FFT& init();

    void operator()(VkCommandBuffer commandBuffer, ComplexSignal& inSignal, ComplexSignal& outSignal);

    void inverse(VkCommandBuffer commandBuffer, ComplexSignal& inSignal, ComplexSignal& outSignal);

    static glm::uvec2 fftImageSize(glm::uvec2 imageSize);

private:
    void createButterflyLookup();

    void createComputeTextures();

    void createDescriptorSets();

    static void prepForCompute(VkCommandBuffer commandBuffer, ComplexSignal& cs);

    static VkImageMemoryBarrier createTransferDstToShaderReadBarrier(Texture& texture);

    static VkImageMemoryBarrier createShaderReadToTransferDstBarrier(Texture& texture);

protected:
    std::vector<PipelineMetaData> pipelineMetaData() override;

private:

    void copy(VkCommandBuffer commandBuffer, ComplexSignal& src, ComplexSignal& dst, glm::uvec2 dim);

    void clear(VkCommandBuffer commandBuffer, ComplexSignal& signal);

    void compute(VkCommandBuffer commandBuffer, Data& signal);

private:


    Data _data;
    Data _inverseData;

    size_t _numPasses{};
    size_t _numSamples{};

    struct {
        int pass{0};
        int N{0};
        int inverse{0};
        int vertical{0};
    } _constants{};

    glm::uvec2 workGroupCount{32};

    VulkanDescriptorSetLayout _signalDescriptorSetLayout;
    VulkanDescriptorSetLayout _lookupDescriptorSetLayout;
    VulkanDescriptorSetLayout _srcSignalDescriptorSetLayout;
    VkDescriptorSet _srcSignalDescriptorSet{};
};