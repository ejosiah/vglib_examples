#include "SubdivisionGrid.hpp"
#include "Barrier.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "filemanager.hpp"
#include <cbt/cbt.hpp>
#include <leb/leb.hpp>
#include <glm/glm.hpp>
#include <imgui.h>

SubdivisionGrid::SubdivisionGrid(VulkanDevice& device, VulkanDescriptorPool& descriptorPool, 
                                 BindlessDescriptor& bindlessDescriptor, const std::string& name,
                                 glm::vec2 resolution, Profiler* profiler, int64 maxDepth)
: m_device{&device}
, m_descriptorPool{&descriptorPool}
, m_bindlessDescriptor{&bindlessDescriptor}
, m_name{name}
, m_resolution{resolution}
, m_profiler{profiler}
, m_maxDepth{maxDepth}
{
    uint should_displace = 1;
    m_sets[1] = bindlessDescriptor.descriptorSet;

    static uint WorkGroupSize = 256;
    static uint cbtID = 0;
    static uint projection_method = 0;
    static uint should_cull_triangle = 0;
    static std::array<uint, 5> entries{ cbtID, WorkGroupSize, should_displace, projection_method, should_cull_triangle};
    specializationConstants = {
            .entries = {
                    {0, sizeof(uint) * 0, sizeof(uint)},
                    {1, sizeof(uint) * 1, sizeof(uint)},
                    {2, sizeof(uint) * 2, sizeof(uint)},
                    {3, sizeof(uint) * 3, sizeof(uint)},
                    {4, sizeof(uint) * 4, sizeof(uint)},
            },
            .data = entries.data(),
            .dataSize = BYTE_SIZE(entries)
    };
}

void SubdivisionGrid::init() {
    initVertexBuffer();
    initBuffers();
    createDescriptorSetLayout();
    updateDescriptorSets();
    createPipelines();
}

void SubdivisionGrid::update(VkCommandBuffer commandBuffer) {
    static int pingPong = 0;

    getCbtInfo(commandBuffer);
    cbtDispatch(commandBuffer);
    subdivide0(commandBuffer, pingPong);
    sumReducePrePass(commandBuffer);
    sumReduceCbt(commandBuffer);
    lebDispatch(commandBuffer);

    pingPong = 1 - pingPong;
}

void SubdivisionGrid::topView(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_topView.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_topView.layout.handle, 0, COUNT(m_sets), m_sets.data(), 0,nullptr);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_emptyBuffer.buffer, &offset);
    vkCmdDrawIndirect(commandBuffer, m_topViewDrawBuffer, 0, 1, sizeof(VkDrawIndirectCommand));
}

void SubdivisionGrid::subdivide0(VkCommandBuffer commandBuffer, int pingPong) {
    withProfiler(queryIds[QUERY_SUBDIVISION_ID], commandBuffer, [&]{
        subdivide(commandBuffer, pingPong);
    });
}

void SubdivisionGrid::cbtDispatch(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("terrain_cbt_dispatcher"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("terrain_cbt_dispatcher"), 0, COUNT(m_sets), m_sets.data(), 0,nullptr);
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    Barriers::pushAndFlush(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
}

void SubdivisionGrid::sumReducePrePass(VkCommandBuffer commandBuffer) {
    withProfiler(queryIds[QUERY_SUM_REDUCE_PRE_PASS_ID], commandBuffer, [&]{
        auto itr = m_maxDepth;
        auto cnt = ((1 << itr) >> 5);
        auto numGroup = (cnt >= 256) ? (cnt >> 8) : 1;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("terrain_cbt_sum_reduce_prepass"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("terrain_cbt_sum_reduce_prepass"), 0, COUNT(m_sets), m_sets.data(), 0,nullptr);
        vkCmdPushConstants(commandBuffer, m_compute.layout("terrain_cbt_sum_reduce_prepass"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &itr);
        vkCmdDispatch(commandBuffer, numGroup, 1, 1);
        Barrier::computeWriteToRead(commandBuffer);
    });
}

void SubdivisionGrid::sumReduceCbt(VkCommandBuffer commandBuffer) {
    withProfiler(queryIds[QUERY_SUM_REDUCE_PRE_PASS_ID], commandBuffer, [&]{
        for(auto itr = m_maxDepth - 6; itr >= 0; --itr) {
            auto cnt = 1 << itr;
            auto numGroup = (cnt >= 256) ? (cnt >> 8) : 1;
            auto pass = itr;

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("terrain_cbt_sum_reduce"));
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("terrain_cbt_sum_reduce"), 0, COUNT(m_sets), m_sets.data(), 0,nullptr);
            vkCmdPushConstants(commandBuffer, m_compute.layout("terrain_cbt_sum_reduce"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &pass);
            vkCmdDispatch(commandBuffer, numGroup, 1, 1);
            Barrier::computeWriteToRead(commandBuffer);
        }
    });
}

void SubdivisionGrid::lebDispatch(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("terrain_leb_dispatcher"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("terrain_leb_dispatcher"), 0, COUNT(m_sets), m_sets.data(), 0,nullptr);
    vkCmdDispatch(commandBuffer, 1, 1, 1);

    static auto dstStageMask = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    static auto dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    Barriers::pushAndFlush(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, dstStageMask , VK_ACCESS_SHADER_WRITE_BIT, dstAccessMask);
}

void SubdivisionGrid::getCbtInfo(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("terrain_cbt_info"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("terrain_cbt_info"), 0, COUNT(m_sets), m_sets.data(), 0,nullptr);
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    Barrier::computeWriteToFragmentRead(commandBuffer);
}

std::vector<PipelineMetaData> SubdivisionGrid::metadata() {
    VulkanDescriptorSetLayout* bindlessDescriptorSetLayout =
            const_cast<VulkanDescriptorSetLayout*>(m_bindlessDescriptor->descriptorSetLayout);

    std::vector<VulkanDescriptorSetLayout*> layouts{ &m_descriptorSetLayout, bindlessDescriptorSetLayout };
    return {
            {
                    .name = fmt::format("{}_leb_dispatcher", m_name),
                    .shadePath = FileManager::resource("leb_dispatcher.comp.spv"),
                    .layouts = layouts,
                    .ranges = {},
                    .specializationConstants = specializationConstants
            },
            {
                    .name = fmt::format("{}_cbt_dispatcher", m_name),
                    .shadePath = FileManager::resource("cbt_dispatcher.comp.spv"),
                    .layouts = layouts,
                    .ranges = {},
                    .specializationConstants = specializationConstants
            },
            {
                    .name = fmt::format("{}_cbt_sum_reduce_prepass", m_name),
                    .shadePath = FileManager::resource("cbt_sum_reduce_prepass.comp.spv"),
                    .layouts = layouts,
                    .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int)} },
                    .specializationConstants = specializationConstants
            },
            {
                    .name = fmt::format("{}_cbt_sum_reduce", m_name),
                    .shadePath = FileManager::resource("cbt_sum_reduce.comp.spv"),
                    .layouts = layouts,
                    .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int)} },
                    .specializationConstants = specializationConstants
            },
            {
                    .name = fmt::format("{}_cbt_info", m_name),
                    .shadePath = FileManager::resource("cbt_info.comp.spv"),
                    .layouts = layouts,
                    .specializationConstants = specializationConstants
            },
            subdivisionMetadata()
    };}

void SubdivisionGrid::createDescriptorSetLayout() {
    m_descriptorSetLayout =
        m_device->descriptorSetLayoutBuilder()
            .name("leb_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(4)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
}

void SubdivisionGrid::updateDescriptorSets() {
    m_descriptorSet = m_descriptorPool->allocate({ m_descriptorSetLayout }).front();

    auto writes = initializers::writeDescriptorSets<5>();

    writes[0].dstSet = m_descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo lebInfo{ m_concurrentBinaryTree, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &lebInfo;

    writes[1].dstSet = m_descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo drawInfo{ m_drawBuffer, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &drawInfo;

    writes[2].dstSet = m_descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo drawTopViewInfo{ m_topViewDrawBuffer, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &drawTopViewInfo;

    writes[3].dstSet = m_descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    VkDescriptorBufferInfo dispatchInfo{ m_dispatchBuffer, 0, VK_WHOLE_SIZE };
    writes[3].pBufferInfo = &dispatchInfo;

    writes[4].dstSet = m_descriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    VkDescriptorBufferInfo cbtInfo{ m_cbtInfo.gpu, 0, VK_WHOLE_SIZE };
    writes[4].pBufferInfo = &cbtInfo;
    
    m_device->updateDescriptorSets(writes);
    m_sets[0] = m_descriptorSet;
}

void SubdivisionGrid::initBuffers() {
    VkDrawIndexedIndirectCommand drawIndexedCmd{(3u << (2 * m_gpuSubDivisions)), 1, 0, 0, 0};
    VkDispatchIndirectCommand dispatchCmd{1, 1, 1};

    auto usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    m_emptyBuffer = m_device->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int) * 3, "empty_buffer");
    m_drawBuffer = m_device->createDeviceLocalBuffer(&drawIndexedCmd, sizeof(VkDrawIndexedIndirectCommand), usage);
    m_topViewDrawBuffer = m_device->createDeviceLocalBuffer(&drawIndexedCmd, sizeof(VkDrawIndirectCommand), usage);
    m_dispatchBuffer = m_device->createDeviceLocalBuffer(&dispatchCmd, sizeof(VkDispatchIndirectCommand), usage);

    m_device->setName<VK_OBJECT_TYPE_BUFFER>(fmt::format("{}_draw_indirect", m_name), m_drawBuffer.buffer);
    m_device->setName<VK_OBJECT_TYPE_BUFFER>(fmt::format("{}_top_view_draw_indirect", m_name), m_topViewDrawBuffer.buffer);
    m_device->setName<VK_OBJECT_TYPE_BUFFER>(fmt::format("{}_dispatch_indirect", m_name), m_dispatchBuffer.buffer);

    auto cbt = cbt::Tree{ static_cast<int64_t>(m_maxDepth), 1};
    auto heap = cbt.getHeap();
    m_concurrentBinaryTree = m_device->createDeviceLocalBuffer(heap.data(), heap.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    m_device->setName<VK_OBJECT_TYPE_BUFFER>(fmt::format("{}_cbt_heap", m_name), m_concurrentBinaryTree.buffer);

    CbtData cbtData{};
    m_cbtInfo.gpu = m_device->createCpuVisibleBuffer(&cbtData, sizeof(CbtData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    m_cbtInfo.cpu = reinterpret_cast<CbtData*>(m_cbtInfo.gpu.map());
    m_device->setName<VK_OBJECT_TYPE_BUFFER>(fmt::format("{}_cbt_info", m_name), m_cbtInfo.gpu.buffer);
}

void SubdivisionGrid::initVertexBuffer() {
    const auto gpuSubd = static_cast<uint64_t>(m_gpuSubDivisions);
    std::vector<uint16_t> cIndexes;
    std::vector<glm::vec2> cVertices;
    std::map<uint32_t, uint16_t> hashMap;
    int lebDepth = 2 * gpuSubd;
    int triangleCount = 1 << lebDepth;
    int edgeTessellationFactor = 1 << gpuSubd;

    for (int i = 0; i < triangleCount; ++i) {
        cbt_Node node = {(uint64_t)(triangleCount + i), 2 * gpuSubd};
        float attribArray[][3] = { {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f} };

        leb_DecodeNodeAttributeArray(node, 2, attribArray);

        for (int j = 0; j < 3; ++j) {
            uint32_t vertexID = attribArray[0][j] * (edgeTessellationFactor + 1)
                                + attribArray[1][j] * (edgeTessellationFactor + 1) * (edgeTessellationFactor + 1);
            auto it = hashMap.find(vertexID);

            if (it != hashMap.end()) {
                cIndexes.push_back(it->second);
            } else {
                uint16_t newIndex = to<uint16_t>(cVertices.size());

                cIndexes.push_back(newIndex);
                hashMap.insert(std::pair<uint32_t, uint16_t>(vertexID, newIndex));
                cVertices.push_back(glm::vec2(attribArray[0][j], attribArray[1][j]));
            }
        }
    }

    m_vertices = m_device->createDeviceLocalBuffer(cVertices.data(), BYTE_SIZE(cVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    m_indexes = m_device->createDeviceLocalBuffer(cIndexes.data(), BYTE_SIZE(cIndexes), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    m_device->setName<VK_OBJECT_TYPE_BUFFER>(fmt::format("{}_vertex_buffer", m_name), m_vertices.buffer);
    m_device->setName<VK_OBJECT_TYPE_BUFFER>(fmt::format("{}_index_buffer", m_name), m_indexes.buffer);
}

void SubdivisionGrid::createPipelines() {
    m_compute = ComputePipelines{ m_device, metadata() };
    m_compute.createPipelines();

    const auto h = m_resolution.y;

    auto bindlessDescriptorSetLayout = const_cast<VulkanDescriptorSetLayout&>(*m_bindlessDescriptor->descriptorSetLayout);
    m_topView.pipeline =
        m_device->graphicsPipelineBuilder()
            .shaderStage()
                .vertexShader(FileManager::resource("empty.vert.spv"))
                .tessellationControlShader(FileManager::resource("top_view.tesc.spv"))
                .tessellationEvaluationShader(FileManager::resource("top_view.tese.spv"))
                .geometryShader(FileManager::resource("top_view.geom.spv"))
                .fragmentShader(FileManager::resource("top_view.frag.spv"))
            .vertexInputState().clear()
            .inputAssemblyState()
                .patches()
            .tessellationState()
                .patchControlPoints(1)
                .domainOrigin(VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT)
            .viewportState().clear()
                .viewport()
                    .origin(10, h - 522)
                    .dimension(512, 512)
                    .minDepth(0)
                    .maxDepth(1)
                .scissor()
                    .offset(10, h - 522)
                    .extent(512, 512)
                .add()
            .rasterizationState()
                .cullNone()
            .depthStencilState()
                .disableDepthWrite()
                .enableDepthTest()
                .compareOpAlways()
                .minDepthBounds(0)
                .maxDepthBounds(1)
            .layout().clear()
                .addDescriptorSetLayout(m_descriptorSetLayout)
                .addDescriptorSetLayout(bindlessDescriptorSetLayout)
            .name("top_view")
        .build(m_topView.layout);
}
