#pragma once

#include "common.h"
#include "ComputePipelins.hpp"
#include "VulkanDescriptorSet.h"
#include <glm/glm.hpp>

struct NormalMapping : public ComputePipelines {
    enum class KernelType : int { Sobel = 0, Scharr };
    struct {
        float dz{54};
        int invertR{1};
        int invertG{1};
        int invertH{1};
        int kernelType{to<int>(KernelType::Sobel)};
        int heightOffset{1};
    } constants;

    NormalMapping() = default;

    NormalMapping(VulkanDevice& device);

    void init();

    void createDescriptorSettLayouts();

    void execute(VkCommandBuffer commandBuffer, VkDescriptorSet srcDescriptorSet, VkDescriptorSet dstDescriptorSet, glm::uvec2 textureSize);

protected:
    std::vector<PipelineMetaData> pipelineMetaData() override;

private:
    VulkanDescriptorSetLayout textureDescriptorSetLayout;
    VulkanDescriptorSetLayout imageDescriptorSetLayout;
    glm::uvec2 workGroupSize{32};
    SpecializationConstants sConstants{};
};