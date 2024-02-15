#pragma once

#include <glm/glm.hpp>
#include <numeric>

struct Bounds {
    glm::vec2 min{std::numeric_limits<float>::max()};
    glm::vec2 max{std::numeric_limits<float>::min()};
};

struct Point {
    glm::vec4 color{1};
    glm::vec2 position{};
    glm::vec2 start{};
    glm::vec2 end{};
    int axis{-1};
    float delta2;
};

struct SearchArea{
    glm::vec2 position{};
    float radius{};
};