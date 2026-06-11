#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"

#include "fluid/FluidSolver2.hpp"
#include "fluid/FieldVisualizer.hpp"

#include <map>
#include <optional>

enum class Scene : uint32_t { Tank, WindTunnel, Paint };

struct SceneProps {
    uint32_t resolution{200};
    uint32_t iterations{40};
    float timeStep{0.016666667f};
    float gravity{9.81};
    std::optional<eular::Quantity> quantity;
};

class YetAnotherFluidSim : public VulkanBaseApp{
public:
    explicit YetAnotherFluidSim(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initSceneProperties();

    void initSolver();

    void initVisualizer();

    eular::ExternalForce gravityForce();

    void initBindlessDescriptor();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initLoader();

    void createComputePipeline();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderUI(VkCommandBuffer cmdBuf);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;

    Scene scene{Scene::Tank};

    bool advance{};

    std::map<Scene, SceneProps> scenes;

    glm::uvec2 solverGridSize{1};
    FieldVisualizer visualizer;
    std::unique_ptr<eular::FluidSolver> solver;

    ComputePipelines compute;
    std::vector<VulkanDescriptorSetLayout> forceFieldSetLayouts;

    struct {
        float gravityY{};
    } gravityConstants;

};
