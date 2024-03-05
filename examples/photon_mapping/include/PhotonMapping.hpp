#include "VulkanRayTraceModel.hpp"
#include "VulkanRayTraceBaseApp.hpp"
#include "shader_binding_table.hpp"
#include "spectrum/spectrum.hpp"
#include "photon.h"

enum ObjectMask: uint32_t  {
    None = 0x00,
    Primitives = 0x01,
    Lights = 0x02,
    Box = 0x04,
    Sphere = 0x08,
    Ceiling = 0x10,
    All = 0xFF,
};

enum HitGroups : uint32_t {
    General = 0,
    Mirror,
    Glass,
    NumHitGroups,
};

enum ShaderIndex : uint32_t {
    RayGen = 0,
    PhotonRayGen,
    DebugRayGen,

    MainMiss,
    LightMiss,
    PhotonMiss,
    DebugMiss,

    MainClosestHit,
    MirrorClosestHit,
    GlassClosestHit,

    PhotonDiffuseHit,
    PhotonMirrorHit,
    PhotonGlassHit,

    DebugClosestHit,

    ShaderCount
};

struct LightData {
    glm::vec4 position;
    glm::vec4 normal;
    glm::vec4 tangent;
    glm::vec4 bitangent;
    glm::vec4 power;
    glm::vec4 lowerCorner;
    glm::vec4 upperCorner;
};

struct KdTree {
    VulkanBuffer gpu;
    int* cpu;
    int size{0};
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

enum Side : uint32_t {
    Root, Left, Right,
};

struct PhotonStats {
    int photonCount{};
    int treeSize{};
};

struct PhotonMapInfo {
    uint32_t mask;
    uint32_t maxBounces;
    uint32_t numPhotons;
    uint32_t neighbourCount;
    float searchDistance;
};

enum class SelectMode : int {
    Position = 0, PositionFound, Area, Off
};

struct DebugData {
    glm::mat4 transform{1};

    glm::vec3 hitPosition;
    float radius{};

    glm::vec3 target{};
    int mode{};

    glm::vec3 cameraPosition{};
    int meshId;

    glm::vec3 pointColor{0, 0, 1};
    int numNeighboursFound;

    float pointSize{1};
    int searchMode{0};
    int numNeighbours{1000};
};

struct DebugInfo {
    VulkanBuffer gpu;
    DebugData* cpu{};
};


constexpr uint32_t MaxPhotons = 1u << 20;
constexpr uint32_t MaxBounces = 5u;

class PhotonMapping : public VulkanRayTraceBaseApp {
public:
    explicit PhotonMapping(const Settings& settings = {});

protected:
    void initApp() override;

    void initKdTree();

    void initDebugData();

    void balanceKdTree();

    void initCamera();

    void loadModel();

    void initLight();

    void loadLTC();

    void initGBuffer();

    void initPhotonMapData();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void updateRtDescriptorSets();

    void updateGBufferDescriptorSet();

    void createCommandPool();

    void createPipelineCache();

    void initCanvas();

    void initRayTraceInfo();

    void createRayTracingPipeline();

    void generatePhotons();

    void rayTrace(VkCommandBuffer commandBuffer);

    void renderPhotons(VkCommandBuffer commandBuffer);

    void rayTraceToCanvasBarrier(VkCommandBuffer commandBuffer) const;

    void CanvasToRayTraceBarrier(VkCommandBuffer commandBuffer) const;

    void createRenderPipeline();

    void createComputePipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderUI(VkCommandBuffer commandBuffer);

    void renderDebug(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void endFrame() override;

    void newFrame() override;

    bool cameraActive();

    void computeIndirectRadiance();

    void computeIndirectRadianceCPU();

    void computeIndirectRadianceCPU2();

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } searchCompute{};

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        struct {
            int iteration{};
            int width{};
            int height{};
            int blockSize{512};
            float radius{};
        } constants{};
        int numIterations{};
        bool run{};
    } computeIR{};

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
        bool enabled{false};
    } debug;

    struct {
        Texture mat;
        Texture amp;
        VulkanSampler sampler;
    } ltc; // Linearly transformed cosine look up tables

    ShaderTablesDescription shaderTablesDesc;
    ShaderBindingTables bindingTables;

    VulkanDescriptorSetLayout lightDescriptorSetLayout;
    VkDescriptorSet lightDescriptorSet;

    VulkanDescriptorSetLayout debugDescriptorSetLayout;
    VkDescriptorSet debugDescriptorSet;

    RayTraceInfo rayTraceInfo;
    Canvas canvas{};

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    VulkanDrawable model;
    Light light;
    glm::vec3 power = spectrum::blackbodySpectrum({4000, 1180}).front();
    std::vector<rt::MeshObjectInstance> instances;
    VulkanBuffer generatedPhotons;
    VulkanBuffer mStagingBuffer;
    VulkanBuffer photonStats;
    VulkanBuffer photonMap;
    VulkanBuffer nearestNeighbourBuffer;
    VulkanBuffer scratchBuffer;
    KdTree kdTree;
    PhotonMapInfo photonMapInfo;
    PhotonStats stats;
    DebugInfo debugInfo;
    bool inspect{};
    bool target{};
    int numNeighbours{200};
    bool computeNearestNeighbour{};
    bool showPhotons{};
    bool photonsReady{};
    std::atomic<float> progress;
    std::vector<mesh::Mesh> meshes;
    SelectMode selectMode{SelectMode::Off};
    chrono::steady_clock::time_point lastCameraUpdate = chrono::steady_clock::now();

    struct {
        Texture color;
        Texture position;
        Texture normal;
        Texture indirectLight;
        VulkanDescriptorSetLayout setLayout;
        VkDescriptorSet descriptorSet;
    } gBuffer;
    std::atomic_int totalDuration{0};

};