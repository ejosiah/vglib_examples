#pragma once

#include "Scene.hpp"
#include "atmosphere/AtmosphereDescriptor.hpp"
#include "filemanager.hpp"
#include "Camera.h"

class Sky {
public:
    explicit Sky(FileManager* fileManager = {},
        VulkanDevice* device = {},
        VulkanDescriptorPool* descriptorPool = {},
        VulkanRenderPass* renderPass = {},
        glm::uvec2 screenDimension = {},
        Camera* camera = {},
        Scene* m_scene = {} );

    void init();

    void render(VkCommandBuffer commandBuffer);

    void update(float time);

    void reload(VulkanRenderPass* newRenderPass, int width, int height);

    const AtmosphereDescriptor& descriptor() const {
        return m_descriptor;
    }

private:

    void createTexture();

    void createSkyBox();

    void createDescriptorSet();

    void updateDescriptorSet();

    void createPipeline();

    void createRenderPipeline();

    void generateSkybox();

    std::string resource(const std::string &name);

private:
    AtmosphereDescriptor m_descriptor;
    FileManager* m_filemanager;
    VulkanDevice* m_device;
    VulkanDescriptorPool* m_pool;
    VulkanRenderPass* m_renderPass;
    glm::uvec3 m_screenDimension;
    Camera* m_camera;

    Texture texture;
    struct {
        VulkanPipeline pipeline;
        VulkanPipelineLayout layout;
    } renderPipeline;
    struct {
        VulkanPipeline pipeline;
        VulkanPipelineLayout layout;
    } generatePipeline;
    std::array<VulkanImageView, 6> views;
    struct {
        VulkanBuffer vertices;
        VulkanBuffer indices;
    } cube;

    VulkanDescriptorSetLayout skyBoxSetLayout;
    VkDescriptorSet skyBoxSet;

    struct Face {
        glm::vec3 direction;
        glm::vec3 up;
    };

    std::array<Face, 6> faces {{
       {{1.0, 0.0, 0.0}, {0.0,-1.0, 0.0}},
       {{-1.0, 0.0, 0.0}, {0.0,-1.0, 0.0}},
       {{0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}},
       {{0.0, -1.0, 0.0}, {0.0, 0.0, -1.0}},
        {{0.0, 0.0, 1.0}, {0.0,-1.0, 0.0}},
        {{0.0, 0.0, -1.0}, {0.0,-1.0, 0.0}}
    }};


    static constexpr uint32_t SKY_BOX_SIZE = 2048;
    Scene* m_scene;
};