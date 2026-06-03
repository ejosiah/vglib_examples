#pragma once

#include "MoonMaterial.hpp"
#include "planet.hpp"

#include <ComputePipelins.hpp>
#include <VulkanDevice.h>

class MoonDeformer
{
public:
    MoonDeformer() = default;
    MoonDeformer(VulkanDevice& device);
    ~MoonDeformer();

    void initialize();

    void apply_deformation(VkCommandBuffer cmd, const Planet& planet, const MoonMaterial& moonMaterial);

private:
    std::vector<PipelineMetaData> metadata();

    void createPipelines();

    VulkanDevice* m_Device{};
    ComputePipelines m_compute;
};
