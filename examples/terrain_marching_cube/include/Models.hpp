#pragma once

#include "VulkanBuffer.h"
#include <AbstractCamera.hpp>
#include <glm/glm.hpp>
#include <array>

struct CameraInfo {
    glm::mat4 view_projection{1};
    glm::mat4 inverse_view_projection{1};
    glm::mat4 grid_to_world;
    Frustum frustum{};
    glm::vec3 position;
    glm::vec3 aabbMin;
    glm::vec3 aabbMax;
};