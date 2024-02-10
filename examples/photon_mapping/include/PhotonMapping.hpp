#include "VulkanRayTraceModel.hpp"
#include "VulkanRayTraceBaseApp.hpp"
#include "shader_binding_table.hpp"
#include "spectrum/spectrum.hpp"

enum ObjectMask: uint32_t  {
    None = 0x00,
    Primitives = 0x01,
    Lights = 0x02,
    Box = 0x04,
    Sphere = 0x08,
    All = 0xFF,
};

enum Object : int {
    Light0 = 0,
    Ceiling,
    RightWall,
    Floor,
    LeftWall,
    TallBox,
    ShortBox,
    BackWall,
    FrontSphere
};

enum HitGroups : uint32_t {
    General = 0,
    Mirror,
    Glass,
    NumHitGroups,
};

enum ShaderIndex : uint32_t {
    RayGen = 0,
    MainMiss,
    LightMiss,

    MainClosestHit,

    MirrorClosestHit,

    GlassClosestHit,

    ShaderCount
};

struct LightData {
    glm::vec4 position;
    glm::vec4 normal;
    glm::vec4 intensity;
    glm::vec4 lowerCorner;
    glm::vec4 upperCorner;
};

struct Light {
    VulkanBuffer gpu;
    LightData* cpu;
};

struct RayTraceData{
    glm::mat4 viewInverse{1};
    glm::mat4 projectionInverse{1};
    uint32_t mask{0xFF};
};

struct RayTraceInfo {
    VulkanBuffer gpu;
    RayTraceData* cpu{};
};

class PhotonMapping : public VulkanRayTraceBaseApp {
public:
    explicit PhotonMapping(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void loadModel();

    void initLight();

    void loadLTC();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void updateRtDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initCanvas();

    void initRayTraceInfo();

    void createRayTracingPipeline();

    void rayTrace(VkCommandBuffer commandBuffer);

    void rasterize(VkCommandBuffer commandBuffer);

    void rayTraceToCanvasBarrier(VkCommandBuffer commandBuffer) const;

    void CanvasToRayTraceBarrier(VkCommandBuffer commandBuffer) const;

    void createRenderPipeline();

    void createComputePipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } compute;

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

    struct {
        Texture mat;
        Texture amp;
        VulkanSampler sampler;
    } ltc; // Linearly transformed cosine look up tables

    ShaderTablesDescription shaderTablesDesc;
    ShaderBindingTables bindingTables;

    VulkanDescriptorSetLayout lightDescriptorSetLayout;
    VkDescriptorSet lightDescriptorSet;

    RayTraceInfo rayTraceInfo;
    Canvas canvas{};

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    VulkanDrawable model;
    Light light;
    glm::vec3 radiance = spectrum::blackbodySpectrum({3000, 1180}).front();
    std::vector<rt::MeshObjectInstance> instances;
};