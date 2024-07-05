#pragma once

#include "Camera.h"
#include "VulkanDevice.h"
#include "Prototypes.hpp"

#include <string>
#include <optional>
#include <string>

class Floor {
public:
    Floor() = default;

    Floor(VulkanDevice& device,
          Prototypes& prototypes,
          const std::optional<std::string>& vertex = {},
          const std::optional<std::string>& fragment = {},
          const std::vector<VulkanDescriptorSetLayout> descriptorSetLayouts = {});

    void init();

    void createVertexBuffer();

    void createPipeline();

    void render(VkCommandBuffer commandBuffer, BaseCameraController& camera, const std::vector<VkDescriptorSet> descriptorSets = {});

private:
    VulkanDevice* _device{};
    Prototypes*_prototypes{};
    VulkanPipeline _pipeline;
    VulkanPipelineLayout _layout;
    VulkanBuffer _vertices;

    std::optional<std::string> _vertexShader;
    std::optional<std::string> _fragmentShader;
    std::vector<VulkanDescriptorSetLayout> _descriptorSetLayouts;
};