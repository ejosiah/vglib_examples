#pragma once

#include "common.h"
#include "constants.hpp"
#include "Camera.h"
#include "Texture.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "filemanager.hpp"
#include <glm/glm.hpp>
#include "scene.hpp"

class Clouds {
public:
    Clouds(const VulkanDevice& device, const VulkanDescriptorPool& descriptorPool, const FileManager& fileManager,
           uint32_t width, uint32_t height, std::shared_ptr<SceneGBuffer> gBuffer, std::shared_ptr<AtmosphereLookupTable> atmosphereLUT);

    void update(const SceneData& sceneData);

    void render(VkCommandBuffer commandBuffer);

    void renderClouds();

    void renderUI(VkCommandBuffer commandBuffer);

private:
    void initUBO();

    void createNoiseTexture();

    void generateNoise();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createPipelines();

    inline const VulkanDevice& device() const {
        return *m_device;
    }

    inline const VulkanDescriptorPool& descriptorPool() const {
        return *m_descriptorPool;
    }

    std::string resource(const std::string& name);

    const VulkanDevice* m_device;
    const VulkanDescriptorPool* m_descriptorPool;
    const FileManager* m_filemanager;

    struct Ubo {
        glm::mat4 projection;
        glm::mat4 view;
        glm::mat4 inverseProjection;
        glm::mat4 inverseView;
        alignas(16) glm::vec3 camera{0};
        alignas(16) glm::vec3 earth_center;
        alignas(16) glm::vec3 sun_direction;
        float innerRadius;
        float outerRadius;
        float earthRadius;
        int viewPortWidth;
        int viewPortHeight;
        float eccentricity;
        float time;
    };

    struct {
        Texture lowFrequencyNoise;
        Texture highFrequencyNoise;
        Texture curlNoise;
    } textures;

    struct {
        Texture coverage;
        Texture cloudType;
        Texture precipitation;
    } weather;

    struct {
        VulkanPipeline pipeline;
        VulkanPipelineLayout layout;
    } cloud;

    uint32_t m_width{0};
    uint32_t m_height{0};
    std::shared_ptr<SceneGBuffer> gBuffer;

    VulkanDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;

    std::shared_ptr<SceneGBuffer> m_gBuffer;

    Ubo* ubo{};
    VulkanBuffer uboBuffer;
    std::shared_ptr<AtmosphereLookupTable> m_atmosphereLUT;
};