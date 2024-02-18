#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <random>
#include <cinttypes>

namespace PoissonDiskSampling {

    constexpr float INV_SQRT2 = 0.70710678118654752440084436210485;

    struct Domain {
        glm::vec2 lower;
        glm::vec2 upper;
    };

    std::vector<glm::vec2> generate(const Domain& domain, float r, uint32_t k = 30, uint32_t seed = 1 << 20);
}