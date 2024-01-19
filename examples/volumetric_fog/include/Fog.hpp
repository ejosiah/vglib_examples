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
    VulkanDescriptorSetLayout uboDescriptorSetLayout;
    VkDescriptorSet uboDescriptorSet;

    VulkanDescriptorSetLayout dataInjectionSetLayout;
    VkDescriptorSet dataInjectionSet;

    VulkanDescriptorSetLayout lightContribSetLayout;
    VkDescriptorSet lightContribSet;

    VulkanDescriptorSetLayout lightScatteringSetLayout;
    VkDescriptorSet lightScatteringSet;

    Texture fogData;
    Texture lightContribution;
    Texture integratedScattering;
};