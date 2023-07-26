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

class SkyDome{
public:
    SkyDome(const VulkanDevice& device, const VulkanDescriptorPool& descriptorPool, const FileManager& fileManager,
            VulkanRenderPass& renderPass, uint32_t width, uint32_t height);

    void update(const SceneData& sceneData);

    void render(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void resize(VulkanRenderPass& renderPass, uint32_t width, uint32_t height);

    float domeHeight{0};

private:

    void initUBO();

    void initVertexBuffers();

    void createDescriptorSetLayout();

    void updateDescriptorSet();

    void createPipelines();

    inline const VulkanDevice& device() const {
        return *m_device;
    }

    inline const VulkanDescriptorPool& descriptorPool() const {
        return *m_descriptorPool;
    }

    inline const VulkanRenderPass& renderPass() const {
        return *m_renderPass;
    }

    std::string resource(const std::string& name);

private:
    const VulkanDevice* m_device{};
    const VulkanDescriptorPool* m_descriptorPool{};
    const FileManager* m_filemanager{};
    VulkanRenderPass* m_renderPass{};

    struct UniformBufferObject {
        glm::mat4 mvp{1};
        alignas(16) glm::vec3 sun{0};
        glm::vec3 eyes{0};
    };

    UniformBufferObject* ubo{};
    VulkanBuffer uboBuffer;

    VulkanDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;

    struct{
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } skyDome;

    uint32_t m_width{0};
    uint32_t m_height{0};

    VulkanBuffer vertexBuffer;
    VulkanBuffer indexBuffer;

};