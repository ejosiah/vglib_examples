#pragma once

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

    void newFrame();

    void preProcess(VkCommandBuffer commandBuffer);

    void render(VkCommandBuffer commandBuffer);

    void renderTopView(VkCommandBuffer commandBuffer);

    void renderToGBuffer(VkCommandBuffer commandBuffer);

    void renderTerrainBruneton(VkCommandBuffer commandBuffer);

    void controls();

    void endFrame();

    uint nodeCount() const;

    void topViewOn();

    void topViewOff();

    void wireOn();

    void wireOff();

    TerrainInfo getInfo() const;

protected:
    void renderTerrain(VkCommandBuffer commandBuffer);

    void renderTerrainDefault(VkCommandBuffer commandBuffer);

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
        float exposure;
        float lodFactor{0};
        float minLodVariance{0.1};
        float dmapFactor{1};
        uint damp_tex_index{~0u};
        uint dmap_normal_tex_index{~0u};
        uint shadow_tex_index{~0u};
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
        float primitivePixelLengthTarget{7};
        float minLodStdev{0};
        float dmapScale{1};
        int gpuSubDivisions{3};
        bool topView{true};
        bool wire{false};
        bool useBruneton{false};
    } m_options;

    Pipeline m_render;
    Pipeline m_renderWire;
    Pipeline m_renderBruneton;
    Pipeline m_renderBrunetonWire;
    Pipeline m_topView;
    Pipeline m_gbuffer;
    std::array<VkDescriptorSet, 2> m_sets;
    SpecializationConstants specializationConstants{};
    uint should_displace = 1;
    AtmosphereModel::Descriptor m_atmosphereDescriptor;
};
