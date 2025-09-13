#pragma once

#include "ContextAware.hpp"
#include "ComputePipelins.hpp"

class DisplacementShadowMap : public ContextAware {
public:
    DisplacementShadowMap(Context& context, const DisplacementMapInfo& displacement, const TerrainInfo& terrain);

    void init();

    void exec(VkCommandBuffer commandBuffer);

    void controls();

    float printPerfStats();

protected:
    void initQueries();

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
    uint m_scale{2};

    struct {
        Frustum frustum;
        glm::vec3  lightDir{1};
        float stepStride{1};
        glm::vec2 heightRange{1};
        glm::vec2 xzScale{1};
        int maxSteps{256};
        float slopeBias{0.001};
        float softness{0.002};
        uint enabled{1};
        uint dmap_tex_index{~0u};
        uint shadow_image_index{~0u};
    } m_Constants{};

    struct {
        float slopeBias{0.01};
        float softness{0.002};
        int maxSteps{256};
        bool enabled{true};
    } m_options{};

    static constexpr int QUERY_SHADOWS_GEN_ID = 0;
    std::vector<std::string> queryIds{ "generate shadows" };
    ComputePipelines m_compute;
};