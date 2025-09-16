#pragma once

#include "ContextAware.hpp"
#include "ComputePipelins.hpp"
#include "AtmosphereModel.hpp"

class Clouds : public ContextAware {
public:
    Clouds(Context& context,AtmosphereModel::Descriptor atmDescriptor);

    void init();

    void newFrame();

    void render(VkCommandBuffer commandBuffer);

    void controls(bool show = true);

    float printPerfStats();

    void endFrame();

protected:
    Context& context() final;

    void initUniforms();

    void initQuery();

    void createCloudShape();

    void createDescriptorSetLayout();

    void updateDescriptorSet();

    void createRenderPipelines();

private:
    Context* m_context{};
    AtmosphereModel::Descriptor m_atmosphereDescriptor;
    Pipeline m_render;
    struct {
        Texture lowFrequency;
        Texture highFrequency;
    } m_shape;

    struct UniformData {
        glm::mat4 viewProjection{1};
        glm::ivec4 mouse{0};
        glm::vec3 windDirection{1, 0, 0};
        float windSpeed{0.1};
        glm::vec3 cameraPosition{0};
        float cloudTopOffset{0.5};
        float cloudMinHeight{1.5};
        float cloudMaxHeight{4};
        float coverage{0.55};
        float cloudType{0};
        float precipitation{0};
        float eccentricity{0.2};
        float scale{5.641};
        float time{0};
        uint detailedSamples{1};
        uint maxSteps{128};
        uint lowFrequencyTexIndex{~0u};
        uint highFrequencyTexIndex{~0u};
    };

    struct {
        VulkanBuffer gpu;
        UniformData* cpu{};
    } m_uniforms;

    VulkanDescriptorSetLayout m_descriptorSetLayout;
    VkDescriptorSet m_descriptorSet{};

    std::string m_query{"cloud render"};
    std::array<VkDescriptorSet, 4> m_sets;
};