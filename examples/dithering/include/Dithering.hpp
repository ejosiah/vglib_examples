#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "ComputePipelins.hpp"

namespace TextureBindingId {
    static constexpr int SourceImage = 0;
    static constexpr int Noise = 1;
};

namespace ImageBindingId {
    static constexpr int DitheredImage = 0;
}


class Dithering : public VulkanBaseApp{
public:
    explicit Dithering(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void loadTextures();

    void createBayerMatrix();

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

    std::vector<PipelineMetaData> pipelineMetaData();

    void renderUI(VkCommandBuffer commandBuffer);

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void dither(VkCommandBuffer commandBuffer);

    void noiseDither(VkCommandBuffer commandBuffer, uint32_t gx, uint32_t gy);

    void orderedDither(VkCommandBuffer commandBuffer, uint32_t gx, uint32_t gy);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    VulkanSampler createNoiseSampler();

    void endFrame() override;

protected:
    enum Target { Gradient, Picture, NumTargets };
    enum  Method { None, WhiteNoise, BlueNoise, Ordered, Floyd_Steinberg, NumMethods };

    static constexpr uint32_t NumBayerMatrix = 7;

    int method = WhiteNoise;
    int target = Picture;
    int bayerMatrixIndex = 0;

    struct {
        glm::vec2 viewportSize{0};
        int grayScale = 1;
        int gammaCorrect = 0;
        int blockSize{};
    } constants;

    std::array<const char*, NumMethods> methods {
        "None", "White Noise", "Blue Noise", "Ordered Dithering", "Floyd-Steinberg Dithering"
    };

    std::array<const char*, NumBayerMatrix> matrixLabel { "2x2", "4x4", "8x8", "16x16", "32x32", "64x64", "128x128" };

    std::array<const char*, NumTargets> targets { "Gradient", "Picture" };

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    std::unique_ptr<ComputePipelines> compute;

    std::vector<VulkanBuffer> bayerMatrixSet;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;

    VulkanDescriptorSetLayout textureDescriptorSetLayout;
    VulkanDescriptorSetLayout imageDescriptorSetLayout;
    VulkanDescriptorSetLayout bayerMatrixBufferDescriptorSet;
    VkDescriptorSet gradientDescriptorSet{};
    VkDescriptorSet pictureDescriptorSet{};
    VkDescriptorSet descriptorSet{};
    std::vector<VkDescriptorSet> bayerMatrixDescriptorSet{};
    Texture picture;
    Texture gradient;
    Texture* source;
    Texture ditheredImage;
    Texture whiteNoise;
    Texture blueNoise;
    Texture whiteTexture;
};