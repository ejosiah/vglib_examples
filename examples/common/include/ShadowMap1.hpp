#pragma once

#include "VulkanDevice.h"
#include "Texture.h"

#include "glm/glm.hpp"
#include <variant>
#include <functional>

class DirectionalShadowMap{};

static constexpr uint32_t SHADOW_MAP_SIZE = 2048;

class OminiDirectionalShadowMap final {
public:
    using Scene = std::function<void()>;

    OminiDirectionalShadowMap() = default;

    explicit OminiDirectionalShadowMap(
            VulkanDevice& device,
            glm::vec3 lightPosition,
            VulkanDescriptorSetLayout meshSetLayout,
            VkDescriptorSet meshSet,
            uint32_t size = SHADOW_MAP_SIZE);

    void init();

    void update(const glm::vec3& lightPosition);

    void capture(Scene&& scene, VkCommandBuffer commandBuffer);

    void modelTransform(VkCommandBuffer commandBuffer, const glm::mat4& matrix);

    const Texture& depth() const;

    const Texture& transmission() const;

private:
    void createShadowMapTexture();

    void initLightSpaceData();

    void createDescriptorSetLayout();

    void updateDescriptorSet();

    void createPipeline();

    void createCube();

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } _pipeline;

    struct {
        VulkanBuffer vertices;
        VulkanBuffer indices;
    } _cube;

    struct {
        glm::mat4 projection{1};
        glm::mat4 view{1};
        glm::mat4 model{1};
        glm::vec3 lightPosition{};
        float farPlane{};
    } _constants;

    struct Face {
        glm::vec3 direction;
        glm::vec3 up;
    };

    static constexpr std::array<Face, 6> Faces {{
       {{1.0, 0.0, 0.0}, {0.0,-1.0, 0.0}},
       {{-1.0, 0.0, 0.0}, {0.0,-1.0, 0.0}},
       {{0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}},
       {{0.0, -1.0, 0.0}, {0.0, 0.0, -1.0}},
       {{0.0, 0.0, 1.0}, {0.0,-1.0, 0.0}},
       {{0.0, 0.0, -1.0}, {0.0,-1.0, 0.0}}
    }};

    Texture _depth;
    Texture _transmission;
    std::array<VulkanImageView, 6> _views;
    std::array<VulkanImageView, 6> _transViews;
    VulkanDescriptorSetLayout _descriptorSetLayout;
    VkDescriptorSet _descriptorSet{};

    struct  {
        VulkanDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSet descriptorSet{};
    } _mesh;

    struct {
        std::array<glm::mat4, 6> cpu;
        VulkanBuffer gpu;
    } _cameras;

    float _depthBiasConstant{1.25f};
    float _depthBiasSlope{1.75f};
    VkImageCreateInfo _imageSpec{};

    uint32_t _size{};
    VulkanDevice* _device;

};

using ShadowMap0 = std::variant<DirectionalShadowMap, OminiDirectionalShadowMap>;
