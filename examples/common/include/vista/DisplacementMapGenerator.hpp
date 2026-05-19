#pragma once

#include "Shared.hpp"
#include "DisplacementMap.hpp"
#include "ComputePipelins.hpp"
#include <string>
#include "ContextAware.hpp"

enum class DisplacementMethod { None, File, FaultFormation, Noise };

class DisplacementMapGenerator {
public:
    DisplacementMapGenerator(Context& context, DisplacementMethod method, uint width, uint height, std::string path = "");

    void init();

    void exec(VkCommandBuffer commandBuffer);

    bool regenerateIfNeeded(VkCommandBuffer commandBuffer);

    bool controls(bool show);

    DisplacementMapInfo displacementMapInfo() const;

    Texture& displacementTexture();

    void refreshDerivedMaps(VkCommandBuffer commandBuffer);

protected:
    void createComputePipelines();

    void loadDisplacementMap();

    void computeFileDisplacementMap(VkCommandBuffer commandBuffer);

    void noneDisplacementMap(VkCommandBuffer commandBuffer);

    void faultFormation(VkCommandBuffer commandBuffer);

    void noiseHeightMap(VkCommandBuffer commandBuffer);

    void blur(VkCommandBuffer commandBuffer);

    void generateNormalMap(VkCommandBuffer commandBuffer);

    void generateSlopeMomentMaps(VkCommandBuffer commandBuffer);

    std::vector<PipelineMetaData> metadata();

    VkDescriptorSet bindlessDescriptorSet();

    VulkanDescriptorSetLayout& bindlessDescriptorSetLayout();

    BindlessDescriptor& bindlessDescriptor();

    VulkanDevice& device();

private:
    struct FileInfo {
        VulkanBuffer pixels;
        int width{};
        int height{};
        int channels{};
    };

    struct {
        glm::vec2 seed{2 << 20, 2 << 21};
        uint maxIterations{100};
        uint iteration{0};
        uint dmap_image_index{~0u};
    } ff_constants;

    struct {
        glm::vec2 seed{2 << 20, 2 << 21};
        uint maxIterations{100};
        bool blur{true};
        int blurIterations{18};
    } ff_options;

    struct NormalGenConstants {
        float bump_strength{};
        float sigma{};
        int sampleRadius{};
        uint dmap_tex_id{};
        uint normal_image_id{};
    };

    struct SlopeMomentConstants {
        float heightScale{1.0f};
        uint dmap_tex_id{};
        uint moments0_image_id{};
        uint moments1_image_id{};
    };

    struct NoiseConstants {
        glm::vec2 seed{137.0f, 941.0f};
        float baseFrequency{2.5};
        float lacunarity{2.0f};
        float gain{0.5f};
        uint octaves{6};
        uint dmap_image_index{~0u};
        uint enableRidges{1};
    } noise_constants;

    Context* m_context;
    std::string m_path;
    DisplacementMap m_displacementMap;
    DisplacementMapInfo m_info;
    DisplacementMethod m_method{DisplacementMethod::File};
    ComputePipelines m_compute;
    FileInfo m_fileInfo;
    bool m_dirty{false};
    uint m_faultFormationImageId{~0u};
    uint m_noiseImageId{~0u};
    uint m_slopeMoments0ImageId{~0u};
    uint m_slopeMoments1ImageId{~0u};
};
