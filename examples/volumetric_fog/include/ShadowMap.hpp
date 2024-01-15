#pragma once

#include "VulkanDevice.h"
#include "Scene.hpp"
#include "Texture.h"
#include "filemanager.hpp"

using CaptureScene = std::function<void(VkCommandBuffer)>;

struct LightSpaceData {
    glm::mat4 viewProj;
};

struct LightSpace {
    VulkanBuffer gpu;
    LightSpaceData* cpu;
};

class ShadowMap {
public:
    explicit ShadowMap(FileManager* fileManager = {}, VulkanDevice* device = {}, VulkanDescriptorPool* descriptorPool = {},Scene* scene = {});

    void init();

    void generate(CaptureScene&& captureScene);

    const Texture& texture() const;


public:

    LightSpace lightSpace;
    VulkanDescriptorSetLayout lightMatrixDescriptorSetLayout;
    VkDescriptorSet lightMatrixDescriptorSet;

    VulkanDescriptorSetLayout shadowMapDescriptorSetLayout;
    VkDescriptorSet shadowMapDescriptorSet;

private:
    void createTextures();

    void initLightSpaceData();

    void createDescriptorSet();

    void updateDescriptorSet();

    void createPipeline();

    void calculateFittingFrustum();

    std::string resource(const std::string &name);

private:
    FileManager* m_filemanager;
    VulkanDevice* m_device{};
    VulkanDescriptorPool* m_pool{};
    Scene* m_scene{};
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } m_pipeline;
    Texture m_texture;
    float depthBiasConstant{1.25f};
    float depthBiasSlope{1.75f};
    VkImageCreateInfo imageSpec{};
    static constexpr uint32_t SHADOW_MAP_SIZE = 2048;
};