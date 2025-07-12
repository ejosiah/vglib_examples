#include "gltf/GltfLoader.hpp"
#include "VulkanRayTraceModel.hpp"
#include "VulkanRayTraceBaseApp.hpp"
#include "shader_binding_table.hpp"

enum class ShaderIndex : int { RayGen, Miss, DiffuseHit, MetalHit, DielectricHit, Implicits, Count };

enum class HitShaders : int { Diffuse, Metal, Dielectric, Count };

enum class RayType : int { Primary, Count };

struct UniformData {
    glm::mat4 viewInverse{1};
    glm::mat4 projInverse{1};
    glm::vec3 cameraPosition;
    uint frame = 0;
    uint maxBounce = 50;
    uint sampleCount = 10000;
    uint currentSample = 0;
    float apertureSize{0};
    float focalDistance{2};
    int adaptiveSampling{1};
    int blueNoise{0};
};

struct DiffuseMaterial {
    glm::vec3 color{0.6};
    int textureId{-1};
};

struct MetalMaterial{
    glm::vec3 color{0.6};
    float roughness{0};
};

struct DielectricMaterial {
    float ior{1.5};
};

class RayTracingWeekendSeries : public VulkanRayTraceBaseApp {
public:
    explicit RayTracingWeekendSeries(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initUniforms();

    void initBindlessDescriptor();

    void createCheckerboardTexture();

    void loadScene();

    void loadDefaultScene();

    void loadInOneWeekendScene();

    void createMaterials();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void endFrame() override;

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initLoader();

    void initCanvas();

    void createRayTracingPipeline();

    void rayTrace(VkCommandBuffer commandBuffer);

    void rayTraceToCanvasBarrier(VkCommandBuffer commandBuffer) const;

    void CanvasToRayTraceBarrier(VkCommandBuffer commandBuffer) const;

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderUI(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void newFrame() override;

    void createNoiseTexture();

    VulkanSampler createNoiseSampler();

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

    Canvas canvas{};

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    std::map<std::string, VulkanDrawable> drawables;
    struct {
        VulkanBuffer gpu;
        UniformData* cpu{};
    } uniforms;

    struct {
        VulkanBuffer diffuse;
        VulkanBuffer metal;
        VulkanBuffer dielectric;
    } materials;
    std::vector<DiffuseMaterial> mattes;
    std::vector<MetalMaterial> metals;
    std::vector<DielectricMaterial> dielectrics;
    VulkanBuffer diffuseSpheres;
    VulkanBuffer diffuseMotion;
    VulkanBuffer metalSpheres;
    VulkanBuffer metalMotion;
    VulkanBuffer dielectricSpheres;
    VulkanBuffer dielectricMotion;
    Texture noise;
    Texture checkerboard;
};