#pragma once

#include "planet.hpp"
#include "WaterData.hpp"

#include <VulkanDevice.h>
#include <ComputePipelins.hpp>

class WaterDeformer
{
public:
    WaterDeformer() = default;
    WaterDeformer(VulkanDevice& device);
    ~WaterDeformer();

    void initialize();

    void apply_deformation(VkCommandBuffer cmd, const Planet& planet, const WaterData& waterData);

private:
    std::vector<PipelineMetaData> metadata();

    void createPipelines();

    // Generic graphics
    VulkanDevice* m_Device{};
    ComputePipelines m_compute;
};
