#pragma once

#include "SubdivisionGrid.hpp"
#include "camera_base.h"
#include "Prototypes.hpp"
#include "ImGuiPlugin.hpp"

class FFTOcean2 :public SubdivisionGrid {
public:
     FFTOcean2(VulkanDevice& device, VulkanDescriptorPool& descriptorPool, BindlessDescriptor& bindlessDescriptor,
              Prototypes& prototypes, BaseCameraController& camera, uint width, uint height);

    void init() final;

    void newFrame();

    void preProcess(VkCommandBuffer commandBuffer);

    void render(VkCommandBuffer commandBuffer);

    void preview(VkCommandBuffer commandBuffer);

    void visualizer(ImGuiPlugin& plugin);

    void renderTopView(VkCommandBuffer commandBuffer);

    void endFrame();

    void controls(bool show = true);

    void updateMouse(glm::ivec2 mouse, int state = 0);

protected:
    PipelineMetaData subdivisionMetadata() final;

    void generateGaussianNoise();

    void generateGaussianNoise(VkCommandBuffer commandBuffer);

    void generateSpectralComponents(VkCommandBuffer commandBuffer);

    void generateSpectralHeightField(VkCommandBuffer commandBuffer);

    void generateTemporalHeightField(VkCommandBuffer commandBuffer);

    void inverseFFT(VkCommandBuffer commandBuffer);

    void inverseFFT(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet);

    void subdivide(VkCommandBuffer commandBuffer, int pingPong) final;

    void visualize(VkCommandBuffer commandBuffer);

    void computeMinMaxHeight(VkCommandBuffer commandBuffer);

    std::vector<PipelineMetaData> additionalMetadata() override;

    void createSimTextures();

    void createPipelines() final;

    void initUniforms();

    void createDescriptorSetLayout() final;

    void updateDescriptorSets() final;

    float computeLodFactor();

private:
    static const uint maxTileSize = 1024;
    static const uint tileSize = 1024;
    constexpr static const float timeScale = to<float>(tileSize)/to<float>(maxTileSize);
    static constexpr uint maxTileCount = 4;
    static constexpr uint tileCount = 4;

    Prototypes* m_prototypes;
    BaseCameraController* m_camera;
    Pipeline m_render;
    Pipeline m_preview;

    struct {
        Texture noise;
        std::array<Texture, 5> staging;
        Texture fftHeightField;
        Texture fftHeightFieldX;
        Texture fftHeightFieldZ;
        Texture fftSlopeX;
        Texture fftSlopeZ;
        Texture heightField;
        Texture normalMap;
        Texture minMax;
    } m_textures;

    struct {
        std::array<std::array<VulkanImageView, tileCount>, 5>  staging;
        std::array<VulkanImageView, tileCount>  fftHeightField;
        std::array<VulkanImageView, tileCount>  fftHeightFieldX;
        std::array<VulkanImageView, tileCount>  fftHeightFieldZ;
        std::array<VulkanImageView, tileCount>  fftSlopeX;
        std::array<VulkanImageView, tileCount>  fftSlopeZ;
        std::array<VulkanImageView, tileCount>  heightField;
        std::array<VulkanImageView, tileCount>  normalMap;
        std::array<VulkanImageView, tileCount>  minMax;
    } m_views;


    struct {
        glm::vec4 windOrientation{-0.79, 1.57, 0.79, 0.0};
        glm::vec4 windSpeed{10, 8, 10, 2};
        glm::vec4 amplitude{0.2};
        glm::vec4 horizontalLength{3348.6, 500.653, 200, 100};
        glm::vec4 windPower{1};
        float time{0};
    } m_controls;

    struct UniformData {
        glm::mat4 modelMatrix{1};
        glm::mat4 modelViewMatrix{1};
        glm::mat4 viewMatrix{1};
        glm::mat4 cameraMatrix{1};
        glm::mat4 viewProjectionMatrix{1};
        glm::mat4 modelViewProjectionMatrix{1};
        std::array<glm::vec4, 6> frustumPlanes;
        glm::vec4 horizontalLength{};
        glm::vec4 camera;
        glm::vec4 lightDirection{1};
        glm::vec4 earthCenter{};
        glm::ivec4 mouse;
        std::array<glm::vec2, maxTileCount> heightMinMax{};
        glm::vec2 dimensions{};
        glm::vec2 sunSize{};
        float lodFactor{0};
        float minLodVariance{0};
        float dmapFactor{1};
        float choppiness{0};
        float rho{0.9};
        float sigma{30};
        float near{0.1};
        float far{1000};
        float normalFallOff{1};
        uint tile{0};
        uint flags{0};
        uint heightMapIndex{~0u};
        uint normalMapIndex{~0u};
    } defaultValues{};

    struct {
        VulkanBuffer gpu;
        UniformData* cpu{};
    } m_uniforms;

    struct {
        float width{52660};
        float height{52660};
    } m_dimensions;

    struct {
        float primitivePixelLengthTarget{7};
        float minLodStdev{0};
        float dmapScale{1};
        float choppiness{1};
        int gpuSubDivisions{3};
        int tile{4};
        bool topView{false};
        bool wire{false};
        bool showTiles{false};
        bool visualizer{false};
        float rho{0.9};
        float sigma{30};
        float lightZenith{15};
        float lightAzimuth{0};
        float normalFallOff{1};
    } m_options;

    VulkanDescriptorSetLayout m_descriptorSetLayout;
    VkDescriptorSet m_descriptorSet{};

    VulkanDescriptorSetLayout m_fftDescriptorSetLayout;
    std::array<VkDescriptorSet, 10>  m_fftDescriptorSet{};

    VulkanDescriptorSetLayout m_uniformsDescriptorSetLayout;
    VkDescriptorSet m_uniformsDescriptorSet{};

    struct {
        struct {
            int view{0};
            int flag{0};
            float scale{0.1};
        } constants;
        Texture texture;
    } m_visualizer;

    uint m_heightMapIndex{~0u};
    uint m_previewIndex{~0u};
    uint m_normalIndex{~0u};
    float m_time{0};
    float m_timePeriod{1.f/120.f};
};