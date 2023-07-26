#pragma once

#include "Texture.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "filemanager.hpp"
#include "scene.hpp"

class ShadowVolumeGenerator{
public:
    std::shared_ptr<ShadowVolume> shadowVolume;

    ShadowVolumeGenerator(const VulkanDevice& device, const VulkanDescriptorPool& descriptorPool,
                          const FileManager& fileManager, uint32_t width, uint32_t height, VulkanRenderPass& renderPass);


    void generate(const SceneData& sceneData, const VulkanBuffer& sourceBuffer, int triangleCount);

    void update(const SceneData& sceneData);

    void render(VkCommandBuffer commandBuffer);

    void resize(VulkanRenderPass& renderPass, uint32_t width, uint32_t height);

    void initAdjacencyBuffers(const VulkanBuffer &sourceBuffer, int triangleCount);

private:
    void createFramebufferAttachments();

    void createFrameBuffer();

    void createDescriptorSetLayout();

    void updateDescriptorSet();

    void createPipelines();

    void initUBO();

    void generateAdjacency(const VulkanBuffer& sourceBuffer, int triangleCount);

    inline const VulkanDevice& device() const {
        return *m_device;
    }

    inline const VulkanDescriptorPool& descriptorPool() const {
        return *m_descriptorPool;
    }

    std::string resource(const std::string& name);

    void initSamplers();


    const VulkanDevice* m_device;
    const VulkanDescriptorPool* m_descriptorPool;
    const FileManager* m_filemanager;
    VulkanRenderPass* m_renderPass;

    uint32_t m_width{0};
    uint32_t m_height{0};

    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
            uint32_t subpass{0};
        } shadowVolumeFront;

        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
            uint32_t subpass{1};
        } shadowVolumeBack;

    } subpasses;

    FramebufferAttachment depthBufferIn;
    FramebufferAttachment depthBufferOut;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } shadow_volume_visual;

    VulkanRenderPass m_shadowVolumeRenderPass;
    VulkanFramebuffer m_shadowVolumeFramebuffer;

    struct {
        VulkanBuffer triangleBuffer;
        VulkanBuffer indexBuffer;
        VulkanBuffer stagingBuffer;
        int numIndices{0};
    } adjacencySupport;

    VulkanDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;

    struct Ubo {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec3 lightPosition;
        glm::vec3 cameraPosition;
    };
    Camera camera;
    Ubo* ubo{};
    VulkanBuffer uboBuffer;
    VulkanSampler valueSampler;

};