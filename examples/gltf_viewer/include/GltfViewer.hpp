#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "Offscreen.hpp"
#include "Bloom.hpp"

struct UniformData {
    Camera camera;
    int brdf_lut_texture_id{};
    int sheen_lut_texture_id{};
    int charlie_lut_texture_id{};
    int irradiance_texture_id{};
    int specular_texture_id{};
    int charlie_env_texture_id{};
    int framebuffer_texture_id{};
    int g_buffer_texture_id{};
    int discard_transmissive{};
    int environment{};
    int tone_map{1};
    int num_lights{1};
    int debug{0};
    int ibl_on{1};
    int direct_on{1};
    float ibl_intensity{1};
};

enum TextureConstants{ BLACK, WHITE, NORMAL, BRDF_LUT, SHEEN_LUT, CHARLIE_LUT, COUNT};

class GltfViewer : public VulkanBaseApp{
public:
    explicit GltfViewer(const Settings& settings = {});

protected:
    void initApp() override;

    void initBloom();

    void initCamera();

    void initScreenQuad();

    void initUniforms();

    void constructModelPaths();

    void createGBuffer();

    void createFrameBufferTexture();

    void createSkyBox();

    void initBindlessDescriptor();

    void createConstantTextures();

    void initModels();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initLoader();

    void createConvolutionSampler();

    void loadTextures();

    void convertToOctahedralMap(Texture& srcTexture, Texture& dstTexture);

    void createIrradianceMap(Texture& srcTexture, Texture& dstTexture);

    void createSpecularMap(Texture& srcTexture, Texture& dstTexture);

    void createCharlieMap(Texture& srcTexture, Texture& dstTexture);

    void updateConvolutionDescriptors(Texture& srcTexture, Texture& dstTexture, VkImageLayout srcImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    void updateSpecularDescriptors(const std::array<VulkanImageView, 6>& imageViews);

    void openFileDialog();

    void createRenderPipeline();

    void createComputePipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void updateUniforms(VkCommandBuffer commandBuffer, VulkanBuffer& uniformBuffer, const UniformData& data);

    void renderToTransmissionFrameBuffer(VkCommandBuffer commandBuffer);

    void renderToGBuffer(VkCommandBuffer commandBuffer);

    void applyBloom(VkCommandBuffer commandBuffer);

    void renderEnvironmentMap(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet, VulkanPipeline* pipeline, VulkanPipelineLayout* layout);

    void renderUI(VkCommandBuffer commandBuffer);

    void renderModel(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet, VulkanPipeline* pipeline, VulkanPipelineLayout* layout, VkBool32 blendingEnabled);

    void toneMap(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void newFrame() override;

    void endFrame() override;

protected:
    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } environmentMap;

        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } pbr;



        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } toneMapper;

    } render;

    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } toOctahedralMap;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } irradiance;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } specular;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } charlie;
    } compute;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    VulkanCommandPool computeCommandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    std::optional<std::filesystem::path> gltfPath;
    VulkanDescriptorSetLayout uniformsDescriptorSetLayout;
    std::vector<Texture> environments;
    std::vector<Texture> stagingTextures;
    std::vector<Texture> irradianceMaps;
    std::vector<Texture> specularMaps;
    std::vector<Texture> charlieMaps;
    struct {
        VulkanBuffer vertices;
        VulkanBuffer indices;
    } skyBox;
    const std::vector<const char*> environmentPaths {
//        "LA_Downtown_Helipad_GoldenHour_3k.hdr",
//        "BasketballCourt_3k.hdr",
//        "old_hall_4k.hdr",
        "HdrOutdoorFrozenCreekWinterDayClear001_JPG_3K.jpg",
//        "HdrOutdoorCityCondoCourtyardEveningClear001_3K.JPG",
//        "HdrOutdoorSnowMountainsEveningClear001_3K.jpg",
    };
    struct {
        int environment{0};
        int envMapType{0};
        int camera{0};
        int debug{0};
        bool directLighting{true};
        bool imageBasedLighting{true};
        float iblIntensity{1};
        int selectedModel;
        bool modelSelected{false};

        struct {
            bool enabled{false};
            int filterId{1};
            int n{2};
            float d0{0.1};
        } bloom;

        std::vector<const char*> models;
        std::vector<const char*> cameras{"Default"};
        std::vector<const char*> debugLabels{
            "Off",
            "Color",
            "Normal",
            "Metalness",
            "Roughness",
            "AmbientOcclusion",
            "Emission"
        };

        std::vector<const char*> bloomFilters{
           "Ideal", "Gaussian", "Butterworth", "Box"
        };
    } options;
    struct {
        VulkanDescriptorSetLayout inDescriptorSetLayout;
        VulkanDescriptorSetLayout outDescriptorSetLayout;
        std::array<VkDescriptorSet, 5> inDescriptorSet{};
        std::array<VkDescriptorSet, 5> outDescriptorSet{};
        VulkanSampler sampler;
    } convolution;

    Offscreen offscreen{};

    struct {
        std::vector<Texture> color;
        std::vector<Texture> depth;
        std::vector<VulkanBuffer> uniforms;
        std::vector<VkDescriptorSet> UniformsDescriptorSet{};
        std::vector<Offscreen::RenderInfo> info{};
    } gBuffer;

    struct  {
        std::vector<Texture> color;
        std::vector<Texture> depth;
        std::vector<VulkanBuffer> uniforms;
        std::vector<VkDescriptorSet> UniformsDescriptorSet{};
        std::vector<Offscreen::RenderInfo> info{};
    } transmissionFramebuffer;
    std::array<std::shared_ptr<gltf::Model>, 2> models{};
    int currentModel{0};
    int bindingOffset{TextureConstants::COUNT};
    int textureSetWidth{4}; // environment + irradiance + specular + charlie
    std::map<std::string, fs::path> modelPaths;
    std::array<Texture, TextureConstants::COUNT> constantTextures;
    UniformData uniforms{};
    VulkanBuffer screenQuad;
    struct {
        bool error{};
        std::string message;
    } fileOpen;
    static constexpr uint32_t lowestLod{4};
    std::vector<Bloom> _bloom;
};