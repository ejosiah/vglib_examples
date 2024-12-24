#pragma once

#include "ComputePipelins.hpp"
#include "VulkanDescriptorSet.h"

class TerrainCompute : public ComputePipelines {
public:
    TerrainCompute() = default;

    TerrainCompute(VulkanDevice& device, std::vector<VulkanDescriptorSetLayout*> descriptorSetLayouts);

    void init();

protected:
    std::vector<PipelineMetaData> pipelineMetaData() override;

private:
    std::vector<VulkanDescriptorSetLayout*> descriptorSetLayouts_;

};