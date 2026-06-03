#pragma once
#include "ComputePipelins.hpp"
#include "constants.hpp"
#include "Texture.h"
#include "VulkanDevice.h"
#include "WaterData.hpp"

class ImGuiPlugin;

class WaterSimulation {
public:
    WaterSimulation() = default;

    WaterSimulation(VulkanDevice& device);

    void initialize();

    void evaluate(VkCommandBuffer commandBuffer, WaterData& waterData);

    void visualizer(ImGuiPlugin& plugin);

protected:

    void createDescriptorSetLayout();

    void updateDescriptorSet();

    void createPipelines();

    std::vector<PipelineMetaData> metadata();

private:
    void dispatch(VkCommandBuffer commandBuffer, const WaterData& waterData, const char* pipeline, uint32_t x, uint32_t y, uint32_t z);

    void generateMipMaps(VkCommandBuffer commandBuffer, WaterData& waterData);

    void visualize(VkCommandBuffer commandBuffer, const WaterData& waterData);

    VulkanDevice* m_device{};
    ComputePipelines m_compute;

    std::array<Texture, g_WaterSimBandCount> m_HImaginaryTexture;
    std::array<Texture, g_WaterSimBandCount> m_FFTRowPassRealTexture;
    std::array<Texture, g_WaterSimBandCount> m_FFTRowPassImaginaryTexture;

    VulkanDescriptorSetLayout m_descriptorSetLayout;
    VkDescriptorSet m_descriptorSet{};

    struct {
        struct {
            int view{};
            int band{};
            float scale{ 0.1f };
        } constants;
        Texture texture;
    } m_visualizer;

};
