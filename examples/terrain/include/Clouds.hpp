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

protected:
    Context& context() final;

    void initUniforms();

    void initQuery();

    void createCloudShape();

private:
    Context* m_context{};
    AtmosphereModel::Descriptor m_atmosphereDescriptor;
    Pipeline m_render;
    struct {
        Texture lowFrequency;
        Texture highFrequency;
    } m_shape;

    struct UniformData {
        uint lowFrequencyTexIndex{~0u};
        uint highFrequencyTexIndex{~0u};
    };

    struct {
        VulkanBuffer gpu;
        UniformData* cpu{};
    } m_uniforms;

    std::string m_query{"cloud render"};
};