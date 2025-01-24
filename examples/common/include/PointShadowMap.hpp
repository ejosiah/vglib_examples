#pragma once
#include "Offscreen.hpp"
#include <functional>

static constexpr uint32_t DEFAULT_SHADOW_MAP_SIZE = 1024;

class PointShadowMap {
public:
    struct ShadowMap {
        Texture color;
        Texture depth;
    };
    using Scene = std::function<void(VkPipelineLayout)>;

    PointShadowMap() = default;

    PointShadowMap(VulkanDevice& device
                   , VulkanDescriptorPool& descriptorPool
                   , uint32_t inflightFrames
                   , VkFormat depthFormat
                   , uint32_t size = DEFAULT_SHADOW_MAP_SIZE);

    void init();

    void update(const glm::vec3& lightPosition);

    void updateWorldMatrix(const glm::mat4& matrix);

    void setRange(float value) const;

    void capture(const Scene& scene, VkCommandBuffer commandBuffer, int currentFrame);

    const Texture& shadowMap(int index) const;

private:
    void createShadowMapTexture();

    void createRenderInfo();

    void createUniforms();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createPipeline();

    VulkanDevice& device();


    Offscreen::RenderInfo& renderInfo(int index);

private:
    VulkanDevice* _device{};
    VulkanDescriptorPool* _descriptorPool{};
    Offscreen _offscreen;
    std::vector<Offscreen::RenderInfo> _renderInfo;
    std::vector<ShadowMap> _shadowMap;
    VulkanDescriptorSetLayout _descriptorSetLayout;
    VulkanDescriptorSetLayout _meshDescriptorSetLayout;
    VkDescriptorSet _descriptorSet{};
    VkFormat _depthFormat{VK_FORMAT_UNDEFINED};
    uint32_t _size{};
    struct {
        glm::mat4 worldTransform{1};
        glm::vec3 lightPosition{};
    } _constants;

    struct {
        VulkanBuffer gpu;
        std::span<glm::mat4> cpu;
    } _uniforms;
    VulkanPipeline _pipeline;
    VulkanPipelineLayout _layout;

    struct Face {
        glm::vec3 direction;
        glm::vec3 up;
    };

    static  std::array<Face, 6> faces;

    static constexpr int PROJECTION_MATRIX = 0;
    static constexpr int VIEW_MATRIX_START = 1;
    static constexpr int DEFAULT_RANGE = 1024;
};