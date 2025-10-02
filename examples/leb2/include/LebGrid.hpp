#pragma once
#include "SubdivisionGrid.hpp"
#include "Prototypes.hpp"

class LebGrid : public SubdivisionGrid {
public:
    LebGrid(VulkanDevice& device, VulkanDescriptorPool& descriptorPool, BindlessDescriptor& bindlessDescriptor, Prototypes& prototypes, uint width, uint height);

    int m_pingPong{};

    void render(VkCommandBuffer commandBuffer);

protected:
    PipelineMetaData subdivisionMetadata() final;

    void subdivide(VkCommandBuffer commandBuffer, int pingPong) final;

    void createPipelines() override;

private:
    Pipeline m_render;
    Prototypes* m_prototypes;
};