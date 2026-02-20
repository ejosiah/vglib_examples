#pragma once

#include <glm/glm.hpp>
#include "Probes.hpp"
#include "ComputePipelins.hpp"
#include "plugins/BindLessDescriptorPlugin.hpp"
#include "shader_binding_table.hpp"
#include "CameraInfo.hpp"

namespace shader_type {
    static constexpr int RayGen = 0;
    static constexpr int Miss = 1;

    static constexpr int ClosesHit = 2;
//        static constexpr int AnyHit = 0;

    static constexpr int Count = 3;
}

namespace rtx {

    class ddgi {
    public:
        struct Params {
            VulkanDevice& device;
            BindlessDescriptor& bindlessDescriptor;
            VulkanDescriptorPool& descriptorPool;
            std::shared_ptr<CameraInfo> cameraInfo;
            VulkanDescriptorSetLayout lightDescriptorSetLayout;
            VulkanDescriptorSetLayout bvhDescriptorSetLayoutLayout;
            VkDescriptorSet lightDescriptorSet{};
            VkDescriptorSet bvhDescriptorSet{};
            uint32_t numLights{0};
            uint32_t depthBufferIndex{~0u};
            uint32_t normalBufferIndex{~0u};
            glm::vec3 sceneHalfWidth{0};
        };

        ddgi() = default;

        explicit ddgi(const Params& p);

        void init();

        void newFrame();

        void exec(VkCommandBuffer commandBuffer);

        void probeRT(VkCommandBuffer commandBuffer);

        void updateIrradiance(VkCommandBuffer commandBuffer);

        void updateVisibility(VkCommandBuffer commandBuffer);

        void sampleIndirect(VkCommandBuffer commandBuffer);

        void endFrame();

        [[nodiscard]]
        Probes probes() const;

        uint indirectLight() const;

    protected:
        void createProbeRtPipeline();

        void createDescriptorSetLayouts();

        void updateDescriptorSet();

        void createBuffers();

        void initTextures();

        void initComputePipelines();

        bool halfResolution() const;

        glm::vec2 getResolution() const;

        std::vector<PipelineMetaData> pipelines();

    private:
        VulkanDevice* m_device{};
        BindlessDescriptor* m_bindlessDescriptor{};
        VulkanDescriptorPool* m_descriptorPool{};
        VulkanDescriptorSetLayout m_lightDescriptorSetLayout;
        VulkanDescriptorSetLayout m_bvhDescriptorSetLayoutLayout;
        VkDescriptorSet m_lightsDescriptorSet{};
        VkDescriptorSet m_bvhDescriptorSet{};

        Probes m_probes;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
            ShaderTablesDescription shaderTablesDesc;
            ShaderBindingTables bindingTables;
            bool enabled{};
        } m_probeRT;

        ComputePipelines m_compute;
        std::shared_ptr<CameraInfo> m_cameraInfo;

        VulkanDescriptorSetLayout m_constantsDescriptorSetLayout;
        VkDescriptorSet m_constantsDescriptorSet{};

        Texture m_indirectLight;
        Texture m_radiance;
        Texture m_irradiance;
        Texture m_probeGridIrradiance;
        Texture m_probeGridVisibility;
        Texture m_probeOffset;

        struct {
            glm::mat4 random_rotation{1};
            glm::vec3  probe_grid_position{};
            glm::vec3  probe_spacing{1};
            glm::vec3  reciprocal_probe_spacing{};
            glm::ivec3 probe_counts{20, 12, 20};
            int   probe_update_offset{0};
            int   probe_update_count{1000};
            int   probe_rays{128};
            int   irradiance_side_length{6};
            int   visibility_side_length{6};
            float hysteresis{0.95};
            float self_shadow_bias{0.3};
            float infinite_bounces_multiplier{0.75};
            uint output_resolution_half{};

            uint depth_texture_index{~0u};
            uint normal_texture_index{~0u};
            uint indirect_texture_index{~0u};
            uint  radiance_texture_index{~0u};
            uint  irradiance_texture_index{~0u};
            uint  visibility_texture_index{~0u};
            uint  probe_offset_texture_index{~0u};

            uint radiance_image_index{~0u};
            uint indirect_image_index{~0u};

            uint num_lights;

            uint  ddgi_debug_options{};
        } m_constants{};

        uint32_t m_raysPerProbe{};
        uint32_t m_numProbes{};

        VulkanBuffer m_constantsBuffer;
        VulkanBuffer m_probeStatus;

        uint32_t m_irradianceAtlasWidth{};
        uint32_t m_irradianceAtlasHeight{};

        uint32_t m_visibilityAtlasWidth{};
        uint32_t m_visibilityAtlasHeight{};

    };
}