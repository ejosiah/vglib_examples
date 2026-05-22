#pragma once

#include "Shared.hpp"
#include "DisplacementMap.hpp"
#include "ComputePipelins.hpp"
#include <array>
#include <string>
#include <vector>
#include "ContextAware.hpp"

enum class DisplacementMethod { None, File, FaultFormation, Noise, FFT, Blend };

class DisplacementMapGenerator {
public:
    DisplacementMapGenerator(Context& context, DisplacementMethod method, uint width, uint height, std::string path = "");

    void init();

    void exec(VkCommandBuffer commandBuffer);

    bool regenerateIfNeeded(VkCommandBuffer commandBuffer);

    bool controls(bool show);

    bool controlsContent();

    DisplacementMapInfo displacementMapInfo() const;

    void setTerrainMetrics(glm::vec2 terrainWorldSize, glm::vec2 heightScale);

    Texture& displacementTexture();

    Texture& normalTexture();

    Texture& slopeMoments0Texture();

    Texture& slopeMoments1Texture();

    void refreshDerivedMaps(VkCommandBuffer commandBuffer);

    fs::path saveTerrainMaps(const fs::path& basePath);

protected:
    void createComputePipelines();

    void loadDisplacementMap();

    void computeFileDisplacementMap(VkCommandBuffer commandBuffer);

    void noneDisplacementMap(VkCommandBuffer commandBuffer);

    void faultFormation(VkCommandBuffer commandBuffer);

    void noiseHeightMap(VkCommandBuffer commandBuffer);

    void fftDisplacementMap(VkCommandBuffer commandBuffer);

    void createFftTextures(VkCommandBuffer commandBuffer, uint fftSize);

    void blendDisplacementMap(VkCommandBuffer commandBuffer);

    void createBlendTextures(VkCommandBuffer commandBuffer);

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
        float heightScaleX{1.0f};
        float heightScaleY{1.0f};
        uint dmap_tex_id{};
        uint normal_image_id{};
    };

    struct SlopeMomentConstants {
        float heightScaleX{1.0f};
        float heightScaleY{1.0f};
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

    struct FftSpectrumConstants {
        glm::vec2 seed{271.0f, 619.0f};
        float amplitude{0.28f};
        float spectralPower{2.0f};
        std::array<float, 6> frequencies{1.0f, 8.0f, 32.0f, 128.0f, 256.0f, 384.0f};
        uint frequencyCount{4};
        uint output_image_index{~0u};
        uint size{0};
    } fft_spectrum_constants;

    struct FftReorderConstants {
        uint input_tex_id{~0u};
        uint output_image_index{~0u};
        uint size{0};
        uint horizontal{1};
    } fft_reorder_constants;

    struct FftPassConstants {
        uint input_tex_id{~0u};
        uint output_image_index{~0u};
        uint size{0};
        uint pass{0};
        uint horizontal{1};
    } fft_pass_constants;

    struct FftDisplacementConstants {
        uint input_tex_id{~0u};
        uint dmap_image_index{~0u};
        uint fftSize{0};
        uint _padding{0};
    } fft_displacement_constants;

    enum class BlendLayerSource : uint { Noise, FFT };

    enum class BlendMode : uint {
        Normal,
        Dissolve,
        Darken,
        Multiply,
        ColorBurn,
        LinearBurn,
        DarkerColor,
        Lighten,
        Screen,
        ColorDodge,
        LinearDodge,
        LighterColor,
        Overlay,
        SoftLight,
        HardLight,
        VividLight,
        LinearLight,
        PinLight,
        HardMix,
        Difference,
        Exclusion,
        Subtract,
        Divide,
        MaxNegativeLayer
    };

    struct BlendLayer {
        bool enabled{true};
        BlendLayerSource source{BlendLayerSource::Noise};
        BlendMode blendMode{BlendMode::Overlay};
        float opacity{0.5f};
        NoiseConstants noise{};
        FftSpectrumConstants fft{};
    };

    struct BlendConstants {
        uint base_tex_id{~0u};
        uint layer_tex_id{~0u};
        uint output_image_index{~0u};
        uint blendMode{0};
        float opacity{1.0f};
        float dissolveSeed{0.0f};
        glm::vec2 _padding{};
    } blend_constants;

    bool blendControls();

    bool blendLayerControls(BlendLayer& layer, int layerIndex);

    bool fftControls(FftSpectrumConstants& constants, uint fftSize);

    void normalizeFftFrequencies(FftSpectrumConstants& constants, uint fftSize);

    void generateBlendLayer(VkCommandBuffer commandBuffer, BlendLayer& layer);

    void dispatchBlend(VkCommandBuffer commandBuffer, uint baseTextureId, uint layerTextureId, uint outputImageId,
                       BlendMode blendMode, float opacity, float dissolveSeed);

    bool stateFileControls();

    void openStateFileDialog();

    void saveState(const fs::path& path) const;

    void loadState(const fs::path& path);

    void setStateStatus(std::string message, bool error = false);

    Context* m_context;
    std::string m_path;
    DisplacementMap m_displacementMap;
    DisplacementMapInfo m_info;
    glm::vec2 m_derivedMapHeightScale{1601.0f / 52660.0f};
    DisplacementMethod m_method{DisplacementMethod::File};
    ComputePipelines m_compute;
    FileInfo m_fileInfo;
    bool m_dirty{false};
    bool m_regenerateFile{false};
    std::array<char, 500> m_stateFilePath{};
    bool m_stateFileDialogOpen{false};
    bool m_stateFileDialogClosed{false};
    std::string m_stateStatus;
    bool m_stateStatusError{false};
    uint m_faultFormationImageId{~0u};
    uint m_noiseImageId{~0u};
    Texture m_fftPing;
    Texture m_fftPong;
    uint m_fftTextureOffset{~0u};
    uint m_fftImageOffset{~0u};
    uint m_fftDisplacementImageId{~0u};
    Texture m_blendLayer;
    std::array<Texture, 2> m_blendAccumulator;
    std::vector<BlendLayer> m_blendLayers;
    uint m_blendTextureOffset{~0u};
    uint m_blendImageOffset{~0u};
    uint m_blendDisplacementImageId{~0u};
    uint m_slopeMoments0ImageId{~0u};
    uint m_slopeMoments1ImageId{~0u};
};
