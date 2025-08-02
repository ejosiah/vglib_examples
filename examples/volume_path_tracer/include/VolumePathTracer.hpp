#include "domain.hpp"
#include "gltf/GltfLoader.hpp"
#include "VulkanRayTraceModel.hpp"
#include "VulkanRayTraceBaseApp.hpp"
#include "shader_binding_table.hpp"

class VolumePathTracer : public VulkanRayTraceBaseApp {
public:
    explicit VolumePathTracer(const Settings& settings = {});

protected:
    void initApp() override;

    void loadModels();

    void initCamera();

    void initBindlessDescriptor();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initLoader();

    void initCanvas();

    void initUniforms();

    void initObjectData();

    void createRayTracingPipeline();

    void rayTrace(VkCommandBuffer commandBuffer);

    void loadEnvironment();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void endFrame() override;

    void newFrame() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        VulkanPipeline pipeline;
        VulkanPipelineLayout layout;
        VulkanDescriptorSetLayout descriptorSetLayout;
        VulkanDescriptorSetLayout instanceDescriptorSetLayout;
        VulkanDescriptorSetLayout vertexDescriptorSetLayout;
        VkDescriptorSet descriptorSet;
        VkDescriptorSet instanceDescriptorSet;
        VkDescriptorSet vertexDescriptorSet;
    } raytrace;

    ShaderTablesDescription shaderTablesDesc;
    ShaderBindingTables bindingTables;

    struct {
        VulkanBuffer gpu;
        UniformData* cpu{};
    } uniforms;

    Canvas canvas{};

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    std::vector<rt::MeshObjectInstance> instances;
    Texture environment;

    struct {
        VulkanDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSet descriptorSet{};
        struct {
            VulkanBuffer gpu;
            MaterialInfo* cpu{};
            int count{0};
        } materials;
        struct {
            VulkanBuffer gpu;
            MediumInfo* cpu{};
            int count{0};
        } mediums;
        struct {
            VulkanBuffer gpu;
            SurfaceInfo* cpu{};
            int count{0};
        } surface;
        const int maxObjects{10};
    } object;

};