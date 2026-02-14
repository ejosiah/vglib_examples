#pragma once

#include "ComputePipelins.hpp"
#include "VulkanDevice.h"
#include "CameraInfo.hpp"
#include "Offscreen.hpp"
#include "plugins/BindLessDescriptorPlugin.hpp"
#include <glm/glm.hpp>
#include <Texture.h>

namespace rtx {
    class shadow {
    public:
        shadow() = default;

        struct Params {
            VulkanDevice& device;
            BindlessDescriptor& bindlessDescriptor;
            VulkanDescriptorPool& descriptorPool;
            std::shared_ptr<CameraInfo> cameraInfo;
            VulkanDescriptorSetLayout lightDescriptorSetLayout;
            VulkanDescriptorSetLayout bvhDescriptorSetLayoutLayout;
            VkDescriptorSet lightDescriptorSet{};
            VkDescriptorSet bvhDescriptorSet{};
            uint32_t numLights;
            uint32_t depthBufferIndex;
            uint32_t normalBufferIndex;
        };

        explicit shadow(const Params& p);

        void init();

        void createDescriptorSetLayouts();

        void updateDescriptorSet();

        void createConstantsBuffer();

        void newFrame();

        void endFrame();

        void exec(VkCommandBuffer commandBuffer);

        void computeMotionVectors(VkCommandBuffer commandBuffer);

        void computeVisibilityVariance(VkCommandBuffer commandBuffer);

        void computeVisibility(VkCommandBuffer commandBuffer);

        void filterVisibility(VkCommandBuffer commandBuffer);

        uint32_t motionVectors() const;

        uint32_t normals() const;

        uint32_t variance() const;

        uint32_t visibility() const;

        uint32_t debug() const;

    private:
        void initTextures();

        void initComputePipelines();

        std::vector<PipelineMetaData> pipelines();

        VulkanDevice* m_device{};
        BindlessDescriptor* m_bindlessDescriptor{};
        VulkanDescriptorPool* m_descriptorPool{};
        VulkanDescriptorSetLayout m_lightDescriptorSetLayout;
        VulkanDescriptorSetLayout m_bvhDescriptorSetLayoutLayout;
        VkDescriptorSet m_lightsDescriptorSet{};
        VkDescriptorSet m_bvhDescriptorSet{};


        uint32_t m_numLights{};
        glm::uvec2 resolution{};
        Texture m_motionVector;
        Texture m_visibilityCache;
        Texture m_variation;
        Texture m_variationCache;
        Texture m_filteredVariation;
        Texture m_filteredVisibility;
        Texture m_sampleCountCache;
        Texture m_debugTexture;

        ComputePipelines m_compute;
        std::shared_ptr<CameraInfo> m_cameraInfo;

        VulkanDescriptorSetLayout m_constantsDescriptorSetLayout;
        VkDescriptorSet m_constantsDescriptorSet{};

        struct {
            float resolution_scale{0.5};
            float resolution_scale_rcp{2};
            uint32_t depthBufferIndex{~0u};
            uint32_t normalsTextureIndex{~0u};

            uint32_t motionVectorTextureIndex{~0u};
            uint32_t visibilityCacheTextureIndex{~0u};
            uint32_t variationTextureIndex{~0u};
            uint32_t variationCacheTextureIndex{~0u};
            uint32_t filteredVariationTextureIndex{~0u};
            uint32_t filteredVisibilityTextureIndex{~0u};
            uint32_t sampleCountCacheTextureIndex{~0u};

            uint32_t motionVectorImageIndex{~0u};
            uint32_t filteredVariationImageIndex{~0u};
            uint32_t filteredVisibilityImageIndex{~0u};
            uint32_t sampleCountCacheImageIndex{~0u};
            uint32_t visibilityCacheImageIndex{~0u};
            uint32_t variationImageIndex{~0u};
            uint32_t variationCacheImageIndex{~0u};
            uint32_t frameIndex{0};
            uint32_t debugImageIndex{~0u};
        } m_constants;

        uint32_t debugTextureIndex{~0u};

        VulkanBuffer m_constantsBuffer;
    };


}