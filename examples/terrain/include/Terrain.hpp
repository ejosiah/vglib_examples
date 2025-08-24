#pragma once

#include "Shared.hpp"
#include "ComputePipelins.hpp"
#include <glm/glm.hpp>
#include <array>
#include <vector>

class Terrain {
public:
    struct CbtData {
        uint maxDepth{0};
        uint nodeCount{0};
    };

    Terrain(Context& context);

    void init();

    void newFrame();

    void preProcess(VkCommandBuffer commandBuffer);

    void render(VkCommandBuffer commandBuffer);

    void controls();

    void endFrame();

    uint nodeCount() const;

    void topViewOn();

    void topViewOff();

    void wireOn();

    void wireOff();

protected:
    void renderTerrain(VkCommandBuffer commandBuffer);

    void renderTopView(VkCommandBuffer commandBuffer);

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

    Context& context();

    VulkanDevice& device();

    BaseCameraController& camera();

    VulkanDescriptorPool& descriptorPool() const;

    GraphicsPipelineBuilder graphicsPipelineBuilder() const;

    float computeLodFactor();

    std::vector<PipelineMetaData> metadata();

    VulkanDescriptorSetLayout& bindlessDescriptorSetLayout();

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
        glm::vec2 resolution{0};
        float lodFactor{0};
        float minLodVariance{0.1};
        float dmapFactor{1};
        uint damp_tex_index{~0u};
        uint dmap_normal_tex_index{~0u};
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
        int gpuSubDivisions{3};
        float primitivePixelLengthTarget{7};
        float minLodStdev{0.1};
        bool topView{true};
        bool wire{true};
        float dmapScale{1};
    } m_options;

    Pipeline m_render;
    Pipeline m_renderWire;
    Pipeline m_topView;
    std::array<VkDescriptorSet, 2> m_sets;
    SpecializationConstants specializationConstants{};
};
