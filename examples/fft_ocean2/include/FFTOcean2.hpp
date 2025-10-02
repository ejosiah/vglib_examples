#pragma once

#include "SubdivisionGrid.hpp"

class FFTOcean2 : public SubdivisionGrid {
public:
    FFTOcean2(VulkanDevice& device, VulkanDescriptorPool& descriptorPool, BindlessDescriptor& bindlessDescriptor,
              Profiler& profiler, glm::vec2 resolution);

    void init() final;

    void preProcess(VkCommandBuffer commandBuffer);

    void preview(VkCommandBuffer commandBuffer);

    void newFrame();

    void endFrame();

protected:
    void createDescriptorSetLayout() override;

    void updateDescriptorSets() override;

    void createSimTextures();

    void createPipelines() override;

    void generateGaussianNoise(VkCommandBuffer commandBuffer);

    void generateSpectralComponents(VkCommandBuffer commandBuffer);

    void generateSpectralHeightField(VkCommandBuffer commandBuffer);

    void generateTemporalHeightField(VkCommandBuffer commandBuffer);

    void inverseFFT(VkCommandBuffer commandBuffer);

    void inverseFFT(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet);

    void subdivide(VkCommandBuffer commandBuffer, int pingPong) override;

    PipelineMetaData subdivisionMetadata() override;

    std::vector<PipelineMetaData> additionalMetadata() override;



private:
    static const uint tileSize = 512;
    static constexpr uint tileCount = 4;
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
    } m_views;


    struct {
        glm::vec4 windOrientation{glm::quarter_pi<float>(), glm::atan(-0.6, -0.4), 0.55, 1.77};
        glm::vec4 windSpeed{40, 6.5, 10, 25};
        glm::vec4 amplitude{4, 0.001, 2, 2.5};
        glm::vec4 horizontalLength{1000, 20, 200, 400};
        glm::vec4 windPower{1};
        float time{0};
    } m_controls;

    Pipeline m_preview;

    VulkanDescriptorSetLayout m_descriptorSetLayout;
    VkDescriptorSet m_descriptorSet{};

    VulkanDescriptorSetLayout m_fftDescriptorSetLayout;
    std::array<VkDescriptorSet, 10>  m_fftDescriptorSet{};

    uint m_previewIndex{~0u};
    float m_time{0};
    float m_timePeriod{1.f/30.f};
};