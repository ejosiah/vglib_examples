#pragma once

#include "VulkanBuffer.h"
#include <AbstractCamera.hpp>
#include <glm/glm.hpp>
#include <array>

struct CameraInfo {
    glm::mat4 view_projection{1};
    glm::mat4 inverse_view_projection{1};
    Frustum frustum{};
    glm::vec3 position;
    glm::vec3 aabbMin;
    glm::vec3 aabbMax;
};

struct CameraView {
    std::vector<VulkanBuffer> gpu;
    std::vector<CameraInfo*> info;
};