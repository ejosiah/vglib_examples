#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"

struct LtcUniforms {
    glm::mat4  view{1};
    glm::vec2 resolution{0};
    int   sampleCount{1};
    float roughness{0.25};
    glm::vec3  dcolor{1};
    glm::vec3  scolor{1};

    float intensity{4};
    float width{8};
    float height{8};
    float roty{0};
    float rotz{0};

    int twoSided{0};
};

class LinearlyTransformedCosines : public VulkanBaseApp{
public:
    explicit LinearlyTransformedCosines(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void loadLtcTextures();

    void initUniforms();

    void initBindlessDescriptor();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initLoader();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void update(float time) override;

    void updateView();

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void renderControls(VkCommandBuffer commandBuffer);

protected:
    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } ltc;
    } render;

    struct {
        VulkanBuffer gpu;
        LtcUniforms* cpu{};
    } uniforms;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    Texture ltc_mag;
    Texture ltc_mat;

    VulkanDescriptorSetLayout ltcDescriptorSetLayout;
    VkDescriptorSet ltcDescriptorSet{};

    VulkanDescriptorSetLayout uniformsDescriptorSetLayout;
    VkDescriptorSet uniformsDescriptorSet{};
    struct {
        float zoom{};
        float rotX{};
        float rotY{};
    } cam;
};