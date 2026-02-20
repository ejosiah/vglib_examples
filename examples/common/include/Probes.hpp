#pragma once

#include <glm/glm.hpp>

struct Probes {
    glm::ivec3 count{20, 12, 20};
    glm::vec3 spacing{1};
    glm::vec3 inverseSpacing{1};
    uint32_t raysPerProbe{128};
    int32_t updateOffsets{0};
    int32_t updateCounts{0};
    float sphereScale{0.1};
    float maxOffset{0.4};
};