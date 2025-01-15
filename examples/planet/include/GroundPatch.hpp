#pragma once

#include "camera_base.h"
#include "VulkanDevice.h"

#include <glm/glm.hpp>
#include <span>
#include <array>

class GroundPatch {
public:
    GroundPatch() = default;

    explicit GroundPatch(VulkanDevice& device, int levels, float scale);

    void init();

    void update(const BaseCameraController& camera);

    const VulkanBuffer& patch() const;

private:
    struct Node {
        std::array<glm::vec3, 4> vertices{};
        bool isLeaf{};
    };

    VulkanDevice* device_{};
    int levels_{1};
    float scale_{1};
    int base2Scale_{2};
    int highest_bit_{0};
    VulkanBuffer patch_buffer_;
    std::span<glm::vec3> patches_;
    std::vector<Node> quad_tree_;
    std::vector<glm::vec3> quad_  {{
        {-.5, 0,  .5}, {.5, 0,  .5},
        {.5, 0, -.5}, {-.5, 0, -.5}
    }};
    std::array<std::vector<glm::vec3>, 4> quadrants_ {{
          {{0, 0, 0}, {0.5, 0, 0}, {0.5, 0, 0.5}, {0, 0, 0.5}},
          {{0.5, 0, 0}, {1, 0, 0}, {1, 0, 0.5}, {0.5, 0, 0.5}},
          {{0, 0, 0.5}, {0.5, 0, 0.5}, {0.5, 0, 1}, {0, 0, 1}},
          {{0.5, 0, 0.5}, {1, 0, 0.5}, {1, 0, 1}, {0.5, 0, 1}}
      }};
};