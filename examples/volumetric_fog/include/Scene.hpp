#pragma once

#include <glm/glm.hpp>
#include <VulkanBuffer.h>
#include <VulkanRAII.h>
#include <tuple>
#include "Camera.h"

struct SceneData {
    glm::vec3 sunDirection;
    float _padding0;
    glm::vec3 sunSize;
    float _padding1;
    glm::vec3 earthCenter;
    float _padding2;
    glm::vec3 camera;
    float exposure;
};

struct Scene {
    VulkanBuffer gpu;
    SceneData* cpu;
    VulkanDescriptorSetLayout sceneSetLayout;
    VkDescriptorSet sceneSet;
    std::tuple<glm::vec3, glm::vec3> bounds;
    Camera* camera;
    float zNear{1};
    float zFar{1};
};