#pragma once

#include "SubdivisionGrid.hpp"
#include "ContextAware.hpp"
#include "ComputePipelins.hpp"
#include "AtmosphereModel.hpp"
#include "Offscreen.hpp"
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

    void controls(bool show = true);

    void endFrame();

    uint nodeCount() const;

    void checkAppInput();

    void topViewOn();

    void topViewOff();

    void wireOn();

    void wireOff();

    TerrainInfo getInfo() const;

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
        uint damp_tex_index{~0u};
        uint dmap_normal_tex_index{~0u};
        uint shadow_tex_index{~0u};
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


    float m_size{52660};

    struct {
        std::string path;
        float width{52660};
        float height{52660};
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

    Pipeline m_render;
    Pipeline m_inspect;
    SpecializationConstants specializationConstants{};
    uint should_displace = 1;
    AtmosphereModel::Descriptor m_atmosphereDescriptor;

    struct {
        glm::vec2 start{1};
        glm::vec2 end{1};
        int state{0};
    } m_inspectConstants;
};
