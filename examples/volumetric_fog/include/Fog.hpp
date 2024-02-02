#pragma once

#include <VulkanBuffer.h>
#include <glm/glm.hpp>

#include <array>

struct FogData {
    glm::mat4 inverseVolumeTransform;

    glm::vec3 boxMin;
    int padding0;

    glm::vec3 boxMax;
    int padding1;

    glm::ivec3 froxelDim;
    float scatteringFactor;

    float constantDensity;
    float heightFogDensity;
    float heightFogFalloff;
    float g;

    float volumeNoisePositionScale;
    float volumeNoiseSpeedScale;
    int applySpatialFiltering;
    int applyTemporalFiltering;

    float temporalFilterBlendWeight;
    float temporalFilterJitterScale;
    glm::vec2 jitter;
};

struct VolumeData {
    glm::mat4 transform;
    glm::mat4 inverseTransform;
};

struct Fog {
    VulkanBuffer gpu;
    FogData* cpu;
    VulkanDescriptorSetLayout uboDescriptorSetLayout;
    VkDescriptorSet uboDescriptorSet;

    Texture fogData;
    std::array<Texture, 2> lightContribution;
    Texture integratedScattering;
};