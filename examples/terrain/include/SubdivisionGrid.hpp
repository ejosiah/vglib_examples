#pragma once

#include "plugins/BindLessDescriptorPlugin.hpp"
#include "ComputePipelins.hpp"
#include "VulkanDevice.h"
#include "Profiler.hpp"

class SubdivisionGrid {
public:
    static constexpr uint SUBDIVISION_BUFFER_BIND_POINT = 5;

    struct CbtData {
        uint maxDepth{0};
        uint nodeCount{0};
    };

    SubdivisionGrid(VulkanDevice& device, VulkanDescriptorPool& descriptorPool, BindlessDescriptor& bindlessDescriptor,
                    const std::string& name, glm::vec2 resolution, uint descriptorCount = 0, Profiler* profiler = {}, int maxDepth = CBT_MAX_DEPTH);

    virtual ~SubdivisionGrid() = default;

    virtual void init();

    void update(VkCommandBuffer commandBuffer);

    void topView(VkCommandBuffer commandBuffer);

protected:
    virtual PipelineMetaData subdivisionMetadata() = 0;

    virtual void subdivide(VkCommandBuffer commandBuffer, int pingPong) = 0;

    virtual void createDescriptorSetLayout();

    virtual void updateDescriptorSets();

    void initQuery();

    void subdivide0(VkCommandBuffer commandBuffer, int pingPong);

    void cbtDispatch(VkCommandBuffer commandBuffer);

    void sumReducePrePass(VkCommandBuffer commandBuffer);

    void sumReduceCbt(VkCommandBuffer commandBuffer);

    void sysDispatch(VkCommandBuffer commandBuffer);

    void getCbtInfo(VkCommandBuffer commandBuffer);

    std::vector<PipelineMetaData> metadata();

    void initBuffers();

    void initVertexBuffer();

    void createPipelines();

    void withProfiler(auto queryId, auto commandBuffer, auto body) {
        if(m_profiler) {
            m_profiler->profile(queryId, commandBuffer, [body](){ body(); });
        }else {
            body();
        }
    }

    static constexpr int CBT_MAX_DEPTH = 25;
    static constexpr int CBT_INIT_MAX_DEPTH = 1;


    VulkanDevice* m_device{};
    VulkanDescriptorPool* m_descriptorPool{};
    BindlessDescriptor* m_bindlessDescriptor{};
    std::string m_name;
    glm::vec2 m_resolution;
    Profiler* m_profiler{};
    int m_maxDepth{CBT_MAX_DEPTH};
    ComputePipelines m_compute;
    VulkanBuffer m_vertices;
    VulkanBuffer m_indexes;
    VulkanBuffer m_emptyBuffer;
    VulkanBuffer m_drawBuffer;
    VulkanBuffer m_topViewDrawBuffer;
    VulkanBuffer m_dispatchBuffer;
    VulkanBuffer m_concurrentBinaryTree;
    VulkanDescriptorSetLayout m_subdGridDescriptorSetLayout;
    VkDescriptorSet m_subdGridDescriptorSet{};
    Pipeline m_topView;
    std::vector<VkDescriptorSet> m_sets;

    int m_gpuSubDivisions{3};
    std::vector<VulkanDescriptorSetLayout*> m_layouts;

    struct {
        VulkanBuffer gpu;
        CbtData* cpu{};
    } m_cbtInfo;

    struct {
        std::string cbtInfo;
        std::string cbtDispatch;
        std::string subdivide;
        std::string sumReducePrepass;
        std::string sumReduce;
        std::string subdivDispatch;
    } pipelines;

    static constexpr int QUERY_SUBDIVISION_ID = 0;
    static constexpr int QUERY_SUM_REDUCE_PRE_PASS_ID = 1;
    static constexpr int QUERY_SUM_REDUCE_ID = 2;
    static constexpr int QUERY_RENDER_ID = 3;
    std::vector<std::string> queryIds{ "subdivision", "sum reduce prePass", "sum reduce", "render" };
};