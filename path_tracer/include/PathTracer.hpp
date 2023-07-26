#include "VulkanRayTraceModel.hpp"
#include "VulkanRayTraceBaseApp.hpp"
#include "shader_binding_table.hpp"


#include "Model.hpp"
#include "Gui.hpp"
#include "vulkan_cuda_interop.hpp"
#include "vulkan_denoiser.hpp"

enum ShaderIndex{
    eRayGen = 0,
    eMiss,
    eLightMiss,

    eClosestHit,
    eOcclusionPrimary,

    eVolumeHit,
    eVolumeAnyHit,
    eOcclusionVolumeHit,
    eOcclusionVolumeAnyHit,

    eGlassHit,
    eGlassOcclusion,
    eShaderCount
};



struct DenoiserGuide{
    Texture albedo;
    Texture normal;
    Texture flow;
};


class PathTracer : public VulkanRayTraceBaseApp {
public:
    explicit PathTracer(const Settings& settings = {});

protected:
    void initApp() final;

    void initShapes();

    void initDenoiser();

    void loadEnvironmentMap();

    void loadMediums();

    void loadModel();

    void loadDragon();

    void initLights();

    void createCornellBox(phong::VulkanDrawableInfo info);

    void initCamera();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initCanvas();

    void createInverseCam();

    void createRayTracingPipeline();

    void createPostProcessPipeline();

    void rayTrace(VkCommandBuffer commandBuffer);

    void denoise();

    void transferImage(VkCommandBuffer commandBuffer);

    void rayTraceToTransferBarrier(VkCommandBuffer commandBuffer) const;

    void transferToRenderBarrier(VkCommandBuffer commandBuffer) const;

    void onSwapChainDispose() final;

    void onSwapChainRecreation() final;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) final;

    void renderUI(VkCommandBuffer commandBuffer);

    void update(float time) final;

    void checkAppInputs() final;

    void cleanup() final;

    void onPause() final;

    void newFrame() final;

    void endFrame() final;

protected:
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
        VulkanPipeline pipeline;
        VulkanPipelineLayout layout;
    } postProcess;

    ShaderTablesDescription shaderTablesDesc;
    ShaderBindingTables bindingTables;

    VulkanBuffer inverseCamProj;
    VulkanBuffer previousInverseCamProj;
    Canvas canvas{};

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<FirstPersonCameraController> camera;

    Model m{};
    Gui gui;

    std::vector<VulkanDrawable*> objects;

    VulkanBuffer lightsBuffer;
    Texture environmentMap;
    Distribution2DTexture envMapDistribution;
    VulkanDescriptorSetLayout envMapDescriptorSetLayout;
    VkDescriptorSet envMapDescriptorSet;

    VulkanDescriptorSetLayout sceneDescriptorSetLayout;
    VkDescriptorSet sceneDescriptorSet;
    static constexpr VkPipelineStageFlags ALL_RAY_TRACE_STAGES =
            VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR |
            VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
            VK_SHADER_STAGE_INTERSECTION_BIT_KHR | VK_SHADER_STAGE_CALLABLE_BIT_KHR;

    std::vector<rt::MeshObjectInstance> instances;
    std::future<void> dragonLoad{};
    Light cornellLight;
    VkPhysicalDeviceSynchronization2Features syncFeatures;
    VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeatures;
    VkPhysicalDeviceRayQueryFeaturesKHR  rayQueryFeatures{};
    VkPhysicalDeviceFeatures2 features2;
    Texture rayTracedTexture;
    Texture gBuffer;

    VulkanDescriptorSetLayout denoiserGuideSetLayout;
    VkDescriptorSet denoiserGuideSet;

    DenoiserGuide denoiserGuide;
    cuda::Semaphore denoiseSemaphore;
    std::vector<VulkanSemaphore> raytraceFinished;
    std::unique_ptr<VulkanDenoiser> denoiser;
    VkTimelineSemaphoreSubmitInfo denoiseTimelineInfo{
            VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO
    };
    uint32_t denoiseAfterFrames = 100;
    uint32_t commandBufferGroups = 4;   // render, raytrace, pre_denoise, post_denoise
    bool shouldDenoise = false;
    uint64_t fenceValue{0};
    std::shared_ptr<OptixContext> optix;

    struct{
        VulkanBuffer rectangles;
        VulkanBuffer spheres;
        VulkanBuffer disks;
        VulkanBuffer polygons;
    } shapes;

    VulkanBuffer lightShapeRef;

    VulkanBuffer mediumBuffer;
};