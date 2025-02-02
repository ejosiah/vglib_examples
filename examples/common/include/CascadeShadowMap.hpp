#pragma once

#include "AbstractCamera.hpp"
#include "Offscreen.hpp"
#include <functional>

class CascadeShadowMap {
public:
    static constexpr uint32_t DEFAULT_SHADOW_MAP_SIZE = 4096;
    static constexpr uint32_t DEFAULT_CASCADE_COUNT = 4;
    static constexpr float DEFAULT_CASCADE_SLIT_LAMBDA = 0.55f;
    using Scene = std::function<void(VulkanPipelineLayout&)>;

    CascadeShadowMap() = default;

    CascadeShadowMap(VulkanDevice& device
    , VulkanDescriptorPool& descriptorPool
    , uint32_t inflightFrames
    , VkFormat depthFormat
    , uint32_t numCascades = DEFAULT_CASCADE_COUNT
    , uint32_t size = DEFAULT_SHADOW_MAP_SIZE);

    void init();

    void update(const AbstractCamera& camera, const glm::vec3& lightDirection, std::span<float> splitDepth, std::span<glm::vec3> extents = {});


    void capture(const Scene& scene, VkCommandBuffer commandBuffer, int currentFrame);

    const Texture& shadowMap(int index) const;

    uint32_t cascadeCount() const;

    void setRenderPass(VulkanRenderPass& renderPass, glm::uvec2 resolution);

    void render(VkCommandBuffer commandBuffer);

    void splitLambda(float value);

    VulkanBuffer cascadeViewProjection() const;

    VulkanDescriptorSetLayout  descriptorSetLayout() const;

    VkDescriptorSet descriptorSet() const;

private:
    void createShadowMapTexture();

    void createImageViews();

    void createRenderInfo();

    void createUniforms();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createPipeline();

    uint32_t viewMask() const;

    VulkanDevice& device();

private:
    VulkanDevice* _device{};
    VulkanDescriptorPool* _descriptorPool{};
    VkFormat _depthFormat{VK_FORMAT_UNDEFINED};
    uint32_t _numCascades;
    uint32_t _size{};
    float _splitLambda{DEFAULT_CASCADE_SLIT_LAMBDA};
    std::vector<float> _cascadeSplits;
    Offscreen _offscreen;
    std::vector<Offscreen::RenderInfo> _renderInfo;
    std::vector<Texture> _shadowMap;
    VulkanDescriptorSetLayout _descriptorSetLayout;
    VulkanDescriptorSetLayout _meshDescriptorSetLayout;
    VkDescriptorSet _descriptorSet{};
    glm::uvec2 _screenResolution{};
    float _depthBiasConstant{0.5f};
    float _depthBiasSlope{1.75f};
    std::array<std::vector<VulkanImageView>, 2> imageViews;

    struct {
        glm::mat4 worldTransform{1};
        int cascadeIndex{0};
    } _constants;

    struct {
        VulkanBuffer gpu;
        std::span<glm::mat4> cpu;
    } _uniforms;
    VulkanBuffer debugBuffer;
    std::span<uint32_t> viewIndexes;
    VulkanPipeline _pipeline;
    VulkanPipelineLayout _layout;
    VulkanRenderPass* _renderPass{};

    struct {
        VulkanPipeline pipeline;
        VulkanPipelineLayout layout;
    } debug;
};