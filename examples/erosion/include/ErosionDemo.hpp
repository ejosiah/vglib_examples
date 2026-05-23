#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "Offscreen.hpp"
#include "vista/Shared.hpp"
#include "vista/Terrain.hpp"
#include "vista/DisplacementMapGenerator.hpp"
#include "vista/DisplacementShadowMap.hpp"
#include "ComputePipelins.hpp"
#include "vista/AtmosphereModel.hpp"
#include "Profiler.hpp"
#include "vista/ErosionSimulator.hpp"
#include "imgui.h"

#include <array>
#include <map>
#include <string>
#include <vector>

class ErosionDemo : public VulkanBaseApp{
public:
    explicit ErosionDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void createSamplers();

    void initCamera();

    void initContext();

    void initGBuffer();

    void initDisplacementMapGenerator();

    void initDisplacementShadowMap();

    void initTerrain();

    void initAtmosphere();

    void initSim();

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

    void newFrame() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void runRenderGraph(VkCommandBuffer commandBuffer);

    void runSim(VkCommandBuffer commandBuffer);

    void renderToDisplay(VkCommandBuffer commandBuffer);

    void textureViewerControls();

    void processTerrainMapSave();

    void processGraphReadback();

    void graphControls();

    void buildGraphSignal();

    void readDisplacementGraphData();

    void backupOriginalTerrain(VkCommandBuffer commandBuffer);

    void applyTerrainMapBinding();

    glm::uvec2 sceneExtent() const;

    void setSceneViewport(VkCommandBuffer commandBuffer) const;

    void toneMap(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    static void localReadBarrier(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void endFrame() override;

    void updateSunPosition();

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        struct {
            int method{3};
            float exposureValue{0};
        } constants;
    } toneMapper;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    Offscreen::RenderInfo renderInfo;

    RenderGraphInputs renderGraphInputs;
    Context context;
    std::unique_ptr<DisplacementMapGenerator> displacementMapGenerator;
    std::unique_ptr<Terrain> terrain;
    std::unique_ptr<DisplacementShadowMap> displacementShadowMap;
    std::unique_ptr<AtmosphereModel> atmosphere;
    std::unique_ptr<ErosionSimulator> erosionSim;
    struct {
        Texture displacement;
        Texture normals;
        Texture slopeMoments0;
        Texture slopeMoments1;
        bool ready{};
    } originalTerrain;
    Texture heightMap;
    Texture normalMap;
    ComputePipelines compute;

    struct {
        int textureSlot{};
        std::map<const Texture*, ImTextureID> imguiTextureIds;
    } textureViewer;

    struct {
        std::array<char, 500> path{"erosion/saves/terrain"};
        bool requested{};
        bool error{};
        std::string status;
    } terrainMapSave;

    struct {
        int axis{};
        float position{0.5f};
        bool visible{};
        bool overlayEnabled{};
        bool readbackRequested{true};
        bool signalDirty{true};
        bool error{};
        uint32_t width{};
        uint32_t height{};
        std::vector<float> heightMap;
        std::vector<float> signalX;
        std::vector<float> signalY;
        glm::vec3 color{1.0f, 0.77f, 0.16f};
        float overlayWidthSamples{4.0f};
        std::string status{"Waiting for graph refresh"};
    } graph;

    glm::vec3 lightDirection;


    VulkanDescriptorSetLayout displayDescriptorSetLayout;
    VkDescriptorSet displayDescriptorSet{};


    struct {
        float lightZenith{15};
        float lightAzimuth{223};
        bool dynamicLight{false};
        bool debug{true};
        bool showOriginalTerrain{};
        bool visualizeWaterFlow{};
        float waterFlowScale{32.0f};
    } options;
    VulkanSampler edgeClampSampler;
    NullProfiler nullProfiler;
    glm::vec2 currentPosition;
};
