#pragma once

#include "common.h"
#include <glm/glm.hpp>
#include "VulkanBaseApp.h"

struct SceneData{
    Camera camera;
    struct {
        glm::vec3 position{0};
        float azimuth{0};
        float elevation{0};
    } sun;
    glm::vec3 eyes;
    float time;
    float fieldOfView{90};
    float zNear{1 * meter};
    float zFar{100000 * km};
    float exposure{10};
    glm::vec3 cameraVelocity{0};
    bool enableLightShaft{false};

};

struct GBuffer{
    FramebufferAttachment position;
    FramebufferAttachment normal;
    FramebufferAttachment albedo;
    FramebufferAttachment material;
    FramebufferAttachment edgeDist;
    FramebufferAttachment depth;
    VulkanDescriptorSetLayout setLayout;
    VkDescriptorSet descriptorSet;
};

struct ShadowVolume{
    FramebufferAttachment shadowIn;
    FramebufferAttachment shadowOut;
    VulkanDescriptorSetLayout setLayout;
    VkDescriptorSet descriptorSet;
};

struct AtmosphereLookupTable {
    Texture irradiance;
    Texture transmittance;
    Texture scattering;
    VulkanDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;
};

struct SceneGBuffer{
    Texture position;
    Texture normal;
    Texture albedo;
    Texture material;
    Texture depth;
    Texture objectType;
    VulkanDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;
};

struct Samplers{
    VulkanSampler nearest;
    VulkanSampler linear;
};