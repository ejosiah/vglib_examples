#pragma once

#include "ComputePipelins.hpp"
#include "VulkanDevice.h"
#include "plugins/BindLessDescriptorPlugin.hpp"
#include "Texture.h"
#include "camera_base.h"
#include <glm/glm.hpp>

namespace taa {

    enum class HistorySamplingFilter : int { Single, CatmullRom  };
    enum class SubSampleFilter : int { None, Michell, BlackmanHarris, CatmullRom };
    enum class HistoryConstraint : int { None, Clip, Clamp, VarianceClip, VarianceClipClamp };

    struct Settings {
        HistorySamplingFilter historySamplingFilter{HistorySamplingFilter::CatmullRom};
        SubSampleFilter subSampleFilter{SubSampleFilter::Michell};
        HistoryConstraint historyConstraint{HistoryConstraint::Clamp};
        bool enableTemporalFiltering{true};
        bool enableInverseLuminanceFiltering{true};
        bool enableLuminanceDifferenceFiltering{true};
        bool fullTaa{true};
        glm::uvec2 resolution{};
    };

    class Taa : public ComputePipelines {
    public:

        Taa(VulkanDevice &device,
            VulkanDescriptorPool& descriptorPool,
            BindlessDescriptor &descriptor,
            Texture &colorBuffer,
            Texture &depthBuffer,
            BaseCameraController& camera,
            glm::vec2& jitter,
            const Settings& settings = {});

        void init();

        void exec(VkCommandBuffer commandBuffer);

        void operator()(VkCommandBuffer commandBuffer);

        void resize(const glm::uvec2& resolution);

        void newFrame();

        void endFrame();

        Settings& settings();

    protected:
        void initTextures();

        void initUniforms();

        void updateBindings();

        void createDescriptorSetLayout();

        void updateDescriptorSets();

        void computeMotionVectors(VkCommandBuffer commandBuffer);

        void resolve(VkCommandBuffer commandBuffer);

        void copyResolveToColorBuffer(VkCommandBuffer commandBuffer);

        std::vector<PipelineMetaData> pipelineMetaData() override;

    private:
        struct UniformData {
            glm::mat4 current_view_projection{};
            glm::mat4 inverse_current_view_projection{};

            glm::mat4 previous_view_projection{};
            glm::mat4 inverse_previous_view_projection{};

            glm::vec2 jitter_xy{};
            glm::vec2 previous_jitter_xy{};

            glm::vec2 resolution{};
            uint32_t color_buffer_index{~0u};
            uint32_t depth_buffer_index{~0u};
            uint32_t velocity_texture_index{~0u};

            uint32_t history_color_texture_index{~0u};
            uint32_t resolve_image_index{~0u};
            uint32_t velocity_image_index{~0u};
        };

        VulkanDevice *m_device{};
        VulkanDescriptorPool* m_descriptorPool{};
        BindlessDescriptor *m_bindlessDescriptor{};
        Texture *m_colorBuffer{};
        Texture *m_depthBuffer{};
        BaseCameraController* m_camera{};
        glm::vec2* m_jitter{};
        Settings m_settings;

        Texture m_velocity;
        std::array<Texture, 2> m_history;
        uint32_t m_resolve{0};
        VulkanBuffer m_constantBuffer;
        VulkanDescriptorSetLayout m_uniformDescriptorSetLayout;
        VkDescriptorSet m_uniformDescriptorSet{};

        struct {
            VulkanBuffer gpu;
            UniformData* cpu;
        } m_uniforms;
    };
}
