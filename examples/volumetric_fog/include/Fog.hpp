#pragma once

#include <VulkanBuffer.h>
#include <glm/glm.hpp>

struct FogData {
    glm::ivec3 froxelDim;
    float scatteringFactor;

    float constantDensity;
    float heightFogDensity;
    float heightFogFalloff;
    float g;
};

struct Fog {
    VulkanBuffer gpu;
    FogData* cpu;
    VulkanDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;

    Texture fogData;
    Texture lightContribution;
    Texture integratedScattering;
};