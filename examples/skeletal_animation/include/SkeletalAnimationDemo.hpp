#include "VulkanBaseApp.h"
#include "model.hpp"
#include "animation.hpp"

class SkeletalAnimationDemo : public VulkanBaseApp{
public:
    explicit SkeletalAnimationDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void initModel();

    void initCamera();

    void createDescriptorPool();

    void createCommandPool();

    void createPipelineCache();

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
    } outline;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } compute;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;

    std::shared_ptr<mdl::Model> model;
    anim::Animation backFlip;
    anim::Character vampire;
//    std::unique_ptr<OrbitingCameraController> cameraController;
    std::unique_ptr<CameraController> cameraController;
    Action* jump;
    Action* dance;
    Action* walk;
};