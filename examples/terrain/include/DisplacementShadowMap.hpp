#pragma once

#include "ContextAware.hpp"
#include "ComputePipelins.hpp"

class DisplacementShadowMap : public ContextAware {
public:
    DisplacementShadowMap(Context& context, const DisplacementMapInfo& displacement, const TerrainInfo& terrain);

    void init();

    void exec(VkCommandBuffer commandBuffer);

protected:
    void createShadowMapTexture();

    void createComputePipelines();

    void initConstants();

    std::vector<PipelineMetaData> metadata();

    Context& context() final;

private:
    Context* m_context{};
    DisplacementMapInfo m_displacementMap;
    TerrainInfo m_terrain;
    Texture m_shadowMap;
    uint m_shadowMapImageIndex{~0u};

    struct {
        glm::vec3  lightDir;     // normalized direction FROM point TOWARD light (e.g., sun)
        float heightScale;  // world units per height [0..1]
        glm::vec2 xzScale;      // world units per texel in X/Z
        float stepStride;   // step size in texels along X/Z (e.g., 1.0)
        int   maxSteps;     // e.g., 256 (tradeoff: quality vs. speed)
        float slopeBias;    // small bias to avoid self-shadowing (e.g., 0.001)
        float softness;     // >0 enables soft shadows; try 0.05..0.2, 0 disables
        uint dmap_tex_index;
        uint shadow_image_index;
    } m_Constants{};

    struct {
        float softness{0.1};
    } m_options{};

    ComputePipelines m_compute;
};