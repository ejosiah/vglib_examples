#pragma once

#include "SubdivisionGrid.hpp"
#include "ContextAware.hpp"
#include "ComputePipelins.hpp"
#include "AtmosphereModel.hpp"
#include "Offscreen.hpp"
#include <glm/glm.hpp>
#include <array>
#include <vector>

class Terrain : public ContextAware {
public:
    struct CbtData {
        uint maxDepth{0};
        uint nodeCount{0};
    };

    explicit Terrain(Context& context, AtmosphereModel::Descriptor atmDescriptor);

    void init();

    void initQuery();

    void newFrame();

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
    void initBuffers();

    void initUniforms();

    void initVertexBuffer();

    void createDescriptorSetLayout();

    void updateDescriptorSets();

    void createRenderPipelines();

    void createComputePipelines();

    void cbtDispatch(VkCommandBuffer commandBuffer);

    void lebSubdivision(VkCommandBuffer commandBuffer, int pingPong);

    void sumReducePrePass(VkCommandBuffer commandBuffer);

    void sumReduceCbt(VkCommandBuffer commandBuffer);

    void lebDispatch(VkCommandBuffer commandBuffer);

    void getCbtInfo(VkCommandBuffer commandBuffer);

    Context& context() final;

    float computeLodFactor();

    std::vector<PipelineMetaData> metadata();

    void loadTerrainTextures();

private:
    static constexpr int64_t CBT_MAX_DEPTH = 25;
    static constexpr int64_t CBT_INIT_MAX_DEPTH = 1;
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
    ComputePipelines m_compute;
    VulkanBuffer m_vertices;
    VulkanBuffer m_indexes;
    VulkanBuffer m_emptyBuffer;
    VulkanBuffer m_drawBuffer;
    VulkanBuffer m_topViewDrawBuffer;
    VulkanBuffer m_dispatchBuffer;
    VulkanBuffer m_concurrentBinaryTree;
    VulkanDescriptorSetLayout m_descriptorSetLayout;
    VkDescriptorSet m_descriptorSet{};

    struct {
        VulkanBuffer gpu;
        UniformData* cpu{};
    } m_uniforms;


    int m_maxDepth{CBT_MAX_DEPTH};
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
        VulkanBuffer gpu;
        CbtData* cpu{};
    } m_cbtInfo;

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
    Pipeline m_renderWire;
    Pipeline m_topView;
    Pipeline m_inspect;
    std::array<VkDescriptorSet, 2> m_sets;
    SpecializationConstants specializationConstants{};
    uint should_displace = 1;
    AtmosphereModel::Descriptor m_atmosphereDescriptor;

    struct {
        glm::vec2 start{1};
        glm::vec2 end{1};
        int state{0};
    } m_inspectConstants;

    static constexpr int QUERY_SUBDIVISION_ID = 0;
    static constexpr int QUERY_SUM_REDUCE_PRE_PASS_ID = 1;
    static constexpr int QUERY_SUM_REDUCE_ID = 2;
    static constexpr int QUERY_RENDER_ID = 3;
    std::vector<std::string> queryIds{ "subdivision", "sum reduce prePass", "sum reduce", "render" };
};
