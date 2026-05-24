#include "constants.hpp"
#include "constant_buffers.hpp"
#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "cbt/large/cbt.h"
#include "cpu_mesh.hpp"
#include "mesh_updater.hpp"
#include "planet.hpp"
#include "cbt/large/cbt_utility.h"

class PlanetDemo : public VulkanBaseApp{
public:
    explicit PlanetDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void createBuffers();

    void initGeometry();

    void initCamera();

    void loadTextures();

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

    void renderSkyBox(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    struct {
        Pipeline primitive;
        Pipeline skybox;
    } render;

    struct {
        VulkanBuffer vertices;
        VulkanBuffer indexes;
    } skybox;

    Texture milkyway;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    CPUMesh planetMesh;

    VulkanDescriptorSetLayout textureDescriptorSetLayout;
    VkDescriptorSet milkywayDescriptorSet{};

    GlobalCB* global;
    VulkanBuffer globalBuffer;

    Planet m_EarthPlanet;
    Planet m_MoonPlanet;
    MeshUpdater m_MeshUpdater;

    VulkanDescriptorSetLayout globalDescriptorSetLayout;
    VkDescriptorSet globalDescriptorSet{};

    CBTType m_CBTType = CBTType::OCBT_128K;
    CBTType m_NewCBTType = CBTType::OCBT_128K;

};
