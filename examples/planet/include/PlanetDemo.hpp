#include "constants.hpp"
#include "constant_buffers.hpp"
#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "FirstPersonCamera.h"
#include "cbt/large/cbt.h"
#include "cpu_mesh.hpp"
#include "earth_renderer.hpp"
#include "mesh_updater.hpp"
#include "planet.hpp"
#include "water_deformer.hpp"
#include "WaterSimulation.hpp"
#include "cbt/large/leb_matrix_cache.h"
#include "cbt/large/cbt_utility.h"

using PlanetCameraController = std::conditional_t<UseDoublePrecisionPlanet, DoubleBaseCameraController, BaseCameraController>;
using PlanetFirstPersonCameraSettings = std::conditional_t<UseDoublePrecisionPlanet, DoubleFirstPersonSpectatorCameraSettings, FirstPersonSpectatorCameraSettings>;
using PlanetFirstPersonCameraController = std::conditional_t<UseDoublePrecisionPlanet, DoubleFirstPersonCameraController, FirstPersonCameraController>;
using PlanetFrustum = std::conditional_t<UseDoublePrecisionPlanet, DoubleFrustum, Frustum>;

class PlanetDemo : public VulkanBaseApp{
public:
    explicit PlanetDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void createBuffers();

    void prepareRender();

    void initGeometry();

    void initCamera();

    void loadTextures();

    void newFrame() override;

    void creatSkyBox();

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

    void renderUI(VkCommandBuffer commandBuffer);

    void renderSkyBox(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void endFrame() override;

    void cleanup() override;

    void onPause() override;

    void updateConstantBuffers();

protected:
    struct {
        Pipeline primitive;
        Pipeline skybox;
    } render;

    struct {
        VulkanBuffer vertices;
        VulkanBuffer indexes;
    } skybox;

    struct {
        VulkanBuffer vertices;
        VulkanBuffer indexes;
    } proxy;

    Texture milkyway;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<PlanetCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    CPUMesh planetMesh;

    VulkanDescriptorSetLayout textureDescriptorSetLayout;
    VkDescriptorSet milkywayDescriptorSet{};

    GlobalCB* global{};
    VulkanBuffer globalBuffer;

    Planet m_EarthPlanet;
    Planet m_MoonPlanet;
    EarthRenderer m_EarthRenderer;
    LebMatrixCache m_LebMatrixCache;
    MeshUpdater m_MeshUpdater;
    WaterData wataData;
    WaterSimulation m_WaterSimulation;
    WaterDeformer m_WaterDeformer;

    VulkanDescriptorSetLayout globalDescriptorSetLayout;
    VkDescriptorSet globalDescriptorSet{};

    CBTType m_CBTType = CBTType::OCBT_128K;
    CBTType m_NewCBTType = CBTType::OCBT_128K;

    // Global rendering properties
    uint32_t m_FrameIndex = 0;
    double m_Time = 0.0;
    glm::vec4 m_ScreenSize = { 0.0, 0.0, 0.0, 0.0 };
    glm::ivec2 m_ScreenSizeI = { 0, 0 };
    bool m_RayTracingPath = false;

    // UI controls
    bool m_DisplayUI{};
    bool m_ActiveUpdate{};
    bool m_ActiveWireFrame{};
    bool m_EnableValidation{};
    bool m_EnableOccupancy{};
    bool advanceFrame{false};
    bool m_ShowWaterVisualizer{false};
    glm::vec3 m_WireframeColor{ 0.6, 0.6, 0.6 };
    float m_WireframeSize{ 0.5 };
    uint32_t m_Occupancy{ 0 };
    UpdateCB m_updateCB;

    bool m_MirrorPOV{true};

};
