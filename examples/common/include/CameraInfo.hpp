#pragma once

#include "VulkanDevice.h"
#include "camera_base.h"

class CameraInfo {
public:
    CameraInfo() = default;

    CameraInfo(VulkanDevice& device, VulkanDescriptorPool& descriptorPool, Camera& camera, uint32_t width, uint32_t height,
               float near, float far);

    void init();

    void createBuffer();

    void newFrame();

    void endFrame();

    VulkanDescriptorSetLayout* descriptorSetLayout();

    const VkDescriptorSet* descriptorSet() const;

    auto& cpu() {
        return *uniforms.cpu;
    }

private:
    void createDescriptorSetLayout();

    void updateDescriptorSet();

    struct Data {
        glm::mat4 projection{1};
         glm::mat4 view{1};
         glm::mat4 model{1};
         glm::mat4 inverseView{1};
         glm::mat4 inverseProjection{1};
         glm::mat4 inverseViewProjection{1};
         glm::mat4 previousViewProjection{1};
         glm::vec2 viewportSize{};
         float near{0.1};
         float far{100};
    };

    VulkanDevice* m_device{};
    VulkanDescriptorPool* m_descriptorPool{};
    Camera* m_camera{};
    Camera m_previous{};

    Data m_defaults{};
    VulkanDescriptorSetLayout m_descriptorSetLayout;
    VkDescriptorSet m_descriptorSet{};

    struct {
        VulkanBuffer gpu;
        Data* cpu{};
    } uniforms;
};