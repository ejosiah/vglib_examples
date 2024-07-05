#include "VulkanBaseApp.h"
#include "AsyncModelLoader.hpp"
#include "imgui.h"
#include "Floor.hpp"

class OminidirectionalShadowMapExample : public VulkanBaseApp{
public:
    explicit OminidirectionalShadowMapExample(const Settings& settings = {});

    enum TextureIDs{ DEPTH_SHADOW_MAP = 0, TRANSMISSION_SHADOW_MAP,  COUNT };

protected:
    void beforeDeviceCreation() override;

    void initApp() override;

    void initCamera();

    void initLights();

    void loadModel();

    void initLoader();

    void initFloor();

    void initShadowMap();

    void updateShadowProjection(const glm::vec3 &lightPosition);

    void initBindlessDescriptor();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void updateMeshDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void captureShadow(VkCommandBuffer commandBuffer);

    void renderModel(VkCommandBuffer commandBuffer);

    void renderLights(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void endFrame() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        glm::vec3 position{0, 2, 0};
    } lamp;

    struct {
        VulkanBuffer vertices;
        VulkanBuffer indices;
        glm::vec3 position{0, 2, 0};
    } light;

    struct {
        Texture depth;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } pipeline;

        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } lamp;
        struct {
            std::array<glm::mat4, 7> cpu;
            VulkanBuffer gpu;
        } cameras;

        struct {
            glm::mat4 projection{1};
            glm::mat4 view{1};
            glm::mat4 model{1};
            glm::vec3 lightPosition{};
            float farPlane{};
        } constants;

        VulkanDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSet descriptorSet{};

        struct Face {
            glm::vec3 direction;
            glm::vec3 up;
        };

        const std::array<Face, 6> Faces {{
                {{1.0, 0.0, 0.0}, {0.0,-1.0, 0.0}},
                {{-1.0, 0.0, 0.0}, {0.0,-1.0, 0.0}},
                {{0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}},
                {{0.0, -1.0, 0.0}, {0.0, 0.0, -1.0}},
                {{0.0, 0.0, 1.0}, {0.0,-1.0, 0.0}},
                {{0.0, 0.0, -1.0}, {0.0,-1.0, 0.0}}
        }};

        const uint32_t size{2048};
    } _shadow;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<FirstPersonCameraController> camera;
    std::unique_ptr<asyncml::Loader> m_loader;
    BindlessDescriptor m_bindLessDescriptor;
//    std::shared_ptr<asyncml::Model> m_model;
    VulkanDescriptorSetLayout m_meshSetLayout;
    VkDescriptorSet m_meshDescriptorSet;
    ImTextureID m_vulkanImageTexId{};
    Floor m_floor;
};