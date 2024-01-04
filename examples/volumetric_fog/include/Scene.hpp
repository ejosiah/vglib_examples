#pragma once

#include <glm/glm.hpp>
#include <VulkanBuffer.h>
#include <VulkanRAII.h>
#include <tuple>
#include "Camera.h"


#define PADDING_4_BYTES(index) char padding##index[4];

struct SceneData {
    glm::mat4 viewProjection;
    glm::mat4 inverseViewProjection;
    glm::mat4 lightViewProjection;
    glm::vec3 sunDirection;
    float exposure;

    glm::vec3 sunSize;
    float znear;

    glm::vec3 earthCenter;
    float zfar;

    glm::vec3 camera;
    PADDING_4_BYTES(0)

    glm::vec3 cameraDirection;
    PADDING_4_BYTES(1)

    glm::vec3 whitePoint;

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