#include "VulkanBaseApp.h"
#include "imgui.h"

struct Signal{
    VulkanBuffer realBuffer;
    VulkanBuffer imaginaryBuffer;
    float* real;
    float* imaginary;
    std::vector<float> xData;
};

struct Signal2D{
    Texture real;
    Texture imaginary;
};

class SignalProcessing : public VulkanBaseApp{
public:
    explicit SignalProcessing(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void createBuffers();

    void createButterflyLookup();

    void initData();

    void loadImageSignal();

    void computeFFT();

    void computeFFTGPU();

    void run2DFFT();

    void clearImages(VkCommandBuffer commandBuffer);

    void compute2DFFT(VkCommandBuffer commandBuffer);

    void compute2DInverseFFT(VkCommandBuffer commandBuffer);

    void compute2DFFT(VkCommandBuffer commandBuffer, std::array<VkDescriptorSet, 3>& sets, std::array<Texture, 2>& signal_real, std::array<Texture, 2>& signal_imaginary);

    void computeLuminance(VkCommandBuffer commandBuffer);

    void prepFFTForRender(VkCommandBuffer commandBuffer);

    void prepInverseFFTForRender(VkCommandBuffer commandBuffer);

    void prepFFTForRender(VkCommandBuffer commandBuffer, const std::array<VkDescriptorSet, 2>& sets, VulkanImage& image, int reverse = 0);

    void computeMask(VkCommandBuffer commandBuffer);

    void applyMask(VkCommandBuffer commandBuffer);

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void createComputeFFTPipeline();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    void newFrame() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void plotGraph(VkCommandBuffer commandBuffer);

    void renderImage();

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    Signal signal;
    Signal frequency;
    Signal polarRep;
    Signal boxFilter;
    Signal SincFilter;
    ImTextureID imageSignalTexId{ };
    ImTextureID imageFourierXformSignalTexId{ };
    ImTextureID maskTextureId{};
    ImTextureID inverseFFTTexId;
    Texture imageSignal;
    Texture fourierRenderImage;
    Texture inverseFFTTexture;
    Texture grayscaleTexture;
    Texture maskTexture;

    struct {
        VulkanPipeline pipeline;
        VulkanPipelineLayout layout;
        VulkanDescriptorSetLayout setLayout;
        VkDescriptorSet descriptorSet;
    } compute_luminance;

    VkDescriptorSet fftDisplayDescriptorSet;
    VkDescriptorSet inverseDisplayDescriptorSet;

    struct {
        VulkanPipeline pipeline;
        VulkanPipelineLayout layout;
        VulkanDescriptorSetLayout setLayout;
        VulkanBuffer maxMagnitudeBuffer;
    } fft_prep_render;

    VulkanDescriptorSetLayout signalDescriptorSetLayout;
    VulkanDescriptorSetLayout lookupDescriptorSetLayout;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline_horizontal;
        VulkanPipeline pipeline_vertical;
    } compute_fft;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } apply_mask;

    struct FFTData {
        std::array<VkDescriptorSet, 2> signalDescriptorSets;
        VkDescriptorSet lookupDescriptorSet;

        struct {
            Texture index;
            Texture lut;
        } butterfly;

        std::array<Texture, 2> signal_real;
        std::array<Texture, 2> signal_imaginary;
    };

    FFTData fftData;
    FFTData inverseFFTData;

    struct {
        VulkanPipeline pipeline;
        VulkanPipelineLayout layout;
        VulkanDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSet descriptorSet{};
        struct {
            int maskId{0};
            int n{2};
            int inverse{0};
            float d0{0.1};
        } constants;
    } compute_mask;

    VkPhysicalDeviceSynchronization2Features syncFeatures;
    std::array<const char*, 10> images{
        "lena.png", "box_01.png", "box_001.png", "box_0001.png"
        ,"circle_01.png", "circle_001.png", "circle_0001.png"
        ,"horizontal_strips_low.png", "horizontal_strips_high.png"
        ,"virtical_strips.png"
    };
    int selectedImage = 0;
    int previousSelectedImage = 0;
    bool recomputeDFT = false;

    static constexpr int N = 512;
};