#pragma once

#include "Camera.h"
#include "VulkanDevice.h"
#include "Prototypes.hpp"

class Floor {
public:
    Floor() = default;

    Floor(VulkanDevice& device, Prototypes& prototypes);

    void init();

    void createVertexBuffer();

    void createPipeline();

    void render(VkCommandBuffer commandBuffer, BaseCameraController& camera);

private:
    VulkanDevice* _device{};
    Prototypes*_prototypes{};
    VulkanPipeline _pipeline;
    VulkanPipelineLayout _layout;
    VulkanBuffer _vertices;
};