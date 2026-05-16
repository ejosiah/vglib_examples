#pragma once

#include "SubdivisionGrid.hpp"
#include "ContextAware.hpp"
#include "ComputePipelins.hpp"
#include "AtmosphereModel.hpp"
#include "Offscreen.hpp"
#include "PrefixSum.hpp"
#include <glm/glm.hpp>
#include <array>
#include <vector>

class Terrain : public SubdivisionGrid, public ContextAware {
public:
    explicit Terrain(Context& context, AtmosphereModel::Descriptor atmDescriptor);

    void init() override;

    void newFrame();

protected:
    PipelineMetaData subdivisionMetadata() final;

    void subdivide(VkCommandBuffer commandBuffer, int pingPong) final;

public:

    void preProcess(VkCommandBuffer commandBuffer);

    void render(VkCommandBuffer commandBuffer);

    void renderTopView(VkCommandBuffer commandBuffer);

    void inspect(VkCommandBuffer commandBuffer);

    void generateNormals(VkCommandBuffer commandBuffer);

    void computeHistogram(VkCommandBuffer commandBuffer);

    void computePartialSum(VkCommandBuffer commandBuffer);

    void reorder(VkCommandBuffer commandBuffer);

    void controls(bool show = true);

    void lightingControls();

    void endFrame();

    uint nodeCount() const;

    void checkAppInput();

    void topViewOn();

    void topViewOff();

    void wireOn();

    void wireOff();

    TerrainInfo getInfo() const;

    float displacementScale() const;

    float printPerfStats();

protected:
    void initUniforms();

    void createDescriptorSetLayout() final;

    void updateDescriptorSets() final;

    void createRenderPipelines();

    void createComputePipelines();

    Context& context() final;

    float computeLodFactor();

    void loadTerrainTextures();

    void initQueryStats();

    void initNormalData();

private:
    struct UniformData {
        glm::mat4 modelMatrix{1};
        glm::mat4 modelViewMatrix{1};
        glm::mat4 viewMatrix{1};
        glm::mat4 cameraMatrix{1};
        glm::mat4 viewProjectionMatrix{1};
        glm::mat4 modelViewProjectionMatrix{1};
        std::array<glm::vec4, 6> frustumPlanes;
        glm::ivec4 mouse{0};
        glm::vec3 lightDirection;
        glm::vec3 whitePoint;
        glm::vec2 resolution;
        glm::vec2 sunSize;
        glm::vec2 tileSize{2.5};
        float exposure;
        float lodFactor{0};
        float minLodVariance{0.1};
        float dmapFactor{1};
        float blendMin{0};
        float blendMax{1};
        uint minArea{*reinterpret_cast<const uint*>(&MAX_FLOAT)};
        uint showTiles{0};
        uint tileColor{0};
        uint wireframeOn{0};
        uint useTriplanerMapping{0};
        uint useLeadrLighting{1};
        uint damp_tex_index{~0u};
        uint dmap_normal_tex_index{~0u};
        uint dmap_slope_moments0_tex_index{~0u};
        uint dmap_slope_moments1_tex_index{~0u};
        uint shadow_tex_index{~0u};
        uint noiseTextureIndex{~0u};
        uint dirtyAlbedoMapIndex{~0u};
        uint dirtyAoMapIndex{~0u};
        uint dirtyRoughnessMapIndex{~0u};
        uint dirtyNormalMapIndex{~0u};
        uint grassAlbedoMapIndex{~0u};
        uint grassAoMapIndex{~0u};
        uint grassRoughnessMapIndex{~0u};
        uint grassNormalMapIndex{~0u};
    } defaultValues{};

    Context* m_context{};
    VulkanDescriptorSetLayout m_descriptorSetLayout;
    VkDescriptorSet m_descriptorSet{};

    struct {
        VulkanBuffer gpu;
        UniformData* cpu{};
    } m_uniforms;


    static constexpr float gridSize{52660};
    static constexpr float halfGridSize{gridSize * 0.5f};

    struct {
        std::string path;
        float width{gridSize};
        float height{gridSize};
        float zMin{-14};
        float zMax{1587.0f};
        float scale{1};
    } m_dmap;

    struct {
        VulkanBuffer normals;
    } m_normalInfo;

    struct {
        float primitivePixelLengthTarget{7};
        float minLodStdev{0};
        float dmapScale{1};
        float tileSize{2.5};
        float blendMin{0};
        float blendMax{1};
        int gpuSubDivisions{3};
        bool topView{false};
        bool wire{false};
        bool triplanerMapping{false};
        bool useLeadrLighting{true};
        bool showTiles{false};
        bool inspect{false};
        int tileColor{0};
    } m_options;

    struct {
        Texture albedoMap;
        Texture aoMap;
        Texture roughnessMap;
        Texture normalMap;
    } dirt;

    struct {
        Texture albedoMap;
        Texture aoMap;
        Texture roughnessMap;
        Texture normalMap;
    } grass;

    Texture m_noise;

    Pipeline m_render;
    Pipeline m_inspect;
    SpecializationConstants specializationConstants{};
    uint should_displace = 1;
    AtmosphereModel::Descriptor m_atmosphereDescriptor;

    PipelineStatsQueryPool m_queryPool;
    uint m_triangleCount{};

    struct {
        Pipeline histogram;
        Pipeline genNormals;

        VulkanBuffer counts;
        VulkanBuffer normals;
        VulkanBuffer counters;

        VulkanDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSet descriptorSet{};

        struct {
            glm::vec3 boundsMin{-halfGridSize, -14, -halfGridSize};
            float threshold{0.3};
            uint tableSize{1 << 20};
        } constants;
    } m_normals;

    PrefixSum m_prefixSum;

    struct {
        glm::vec2 start{1};
        glm::vec2 end{1};
        int state{0};
    } m_inspectConstants;
};
