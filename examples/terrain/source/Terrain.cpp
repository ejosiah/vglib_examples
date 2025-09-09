#include "Terrain.hpp"
#include "Barrier.hpp"

#include <cbt/cbt.hpp>
#include <leb/leb.hpp>
#include <glm/glm.hpp>
#include <imgui.h>
#include "AppContext.hpp"

Terrain::Terrain(Context &context, AtmosphereModel::Descriptor atmDescriptor)
: m_context{&context}
, m_atmosphereDescriptor(atmDescriptor)
{
    m_sets[1] = context.bindlessDescriptor->descriptorSet;
//    m_sets[2] = atmDescriptor.set;

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

void Terrain::init() {
    initUniforms();
    initVertexBuffer();
    initBuffers();
    createDescriptorSetLayout();
    updateDescriptorSets();
    createRenderPipelines();
    createComputePipelines();
}

void Terrain::newFrame() {

    glm::mat4 model = m_uniforms.cpu->modelMatrix;
    auto mvp = m_context->viewProjection * model;
    m_uniforms.cpu->modelViewMatrix = m_context->view * model;
    m_uniforms.cpu->viewMatrix = m_context->view;
    m_uniforms.cpu->cameraMatrix = glm::inverse(m_context->view);
    m_uniforms.cpu->viewProjectionMatrix = m_context->viewProjection;
    m_uniforms.cpu->modelViewProjectionMatrix = mvp;
    m_uniforms.cpu->lodFactor = computeLodFactor();
    m_uniforms.cpu->dmapFactor = m_options.dmapScale;
    m_uniforms.cpu->minLodVariance = std::sqrt(m_options.minLodStdev / 64.f / m_options.dmapScale);
    m_uniforms.cpu->lightDirection = context().lightDirection;
    m_uniforms.cpu->exposure = context().exposure;
    m_uniforms.cpu->mouse = context().mouse;
    m_options.useBruneton = context().useBruneton;
    static Frustum frustum;
    Frustum::extractFrustum(frustum, mvp);
    std::memcpy(m_uniforms.cpu->frustumPlanes.data(), frustum.cp.data(), BYTE_SIZE(frustum.cp));
}

void Terrain::preProcess(VkCommandBuffer commandBuffer) {
    static int pingPong = 0;

    getCbtInfo(commandBuffer);
    cbtDispatch(commandBuffer);
    lebSubdivision(commandBuffer, pingPong);
    sumReducePrePass(commandBuffer);
    sumReduceCbt(commandBuffer);
    lebDispatch(commandBuffer);

    pingPong = 1 - pingPong;
}


void Terrain::render(VkCommandBuffer commandBuffer) {
    renderTerrain(commandBuffer);
}

void Terrain::renderTerrain(VkCommandBuffer commandBuffer) {
    renderTerrainDefault(commandBuffer);
}

void Terrain::renderTerrainDefault(VkCommandBuffer commandBuffer) {
    auto pipeline = m_options.wire ? m_renderWire.pipeline.handle : m_render.pipeline.handle;
    auto layout = m_options.wire ? m_renderWire.layout.handle : m_render.layout.handle;

    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = m_sets[0];
    sets[1] = m_sets[1];
    sets[2] = m_atmosphereDescriptor.set;

    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, COUNT(sets), sets.data(), 0,nullptr);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertices.buffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, m_indexes, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexedIndirect(commandBuffer, m_drawBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
}

void Terrain::renderTerrainBruneton(VkCommandBuffer commandBuffer) {
    auto pipeline = m_options.wire ? m_renderBrunetonWire.pipeline.handle : m_renderBruneton.pipeline.handle;
    auto layout = m_options.wire ? m_renderBrunetonWire.layout.handle : m_renderBruneton.layout.handle;

    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = m_sets[0];
    sets[1] = m_sets[1];
    sets[2] = AppContext::atmosphere().descriptor.uboDescriptorSet;
    sets[3] = AppContext::atmosphere().descriptor.lutDescriptorSet;

    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, COUNT(sets), sets.data(), 0,nullptr);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertices.buffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, m_indexes, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexedIndirect(commandBuffer, m_drawBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
}


void Terrain::renderToGBuffer(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gbuffer.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gbuffer.layout.handle, 0, COUNT(m_sets), m_sets.data(), 0,nullptr);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertices.buffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, m_indexes, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexedIndirect(commandBuffer, m_drawBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
}

void Terrain::renderTopView(VkCommandBuffer commandBuffer) {
    if(!m_options.topView) return;

    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_topView.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_topView.layout.handle, 0, COUNT(m_sets), m_sets.data(), 0,nullptr);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_emptyBuffer.buffer, &offset);
    vkCmdDrawIndirect(commandBuffer, m_topViewDrawBuffer, 0, 1, sizeof(VkDrawIndirectCommand));
}

void Terrain::initUniforms() {
    defaultValues.damp_tex_index = context().dmap_tex_index;
    defaultValues.dmap_normal_tex_index = context().dmap_normal_tex_index;
    defaultValues.shadow_tex_index = context().dmap_shadow_tex_index;
    defaultValues.sunSize = AppContext::atmosphere().info.cpu->sunSize;
    defaultValues.whitePoint = AppContext::atmosphere().info.cpu->whitePoint;
    defaultValues.exposure = AppContext::atmosphere().info.cpu->exposure;

    const float width = m_dmap.width;
    const float height = m_dmap.height;
    const float zMin = m_dmap.zMin;
    const float zMax = m_dmap.zMax;

    glm::mat4 model = glm::mat4{1};
    model = glm::translate(model, {0, -zMin, 0});
    model = glm::scale(model, {width, zMax - zMin, height});
    model = glm::rotate(model, -glm::half_pi<float>(), {1, 0, 0});
    model = glm::translate(model, {-0.5f, -0.5f, 0.0f});
    defaultValues.modelMatrix = model;
    defaultValues.resolution = { m_context->screenWidth, m_context->screenHeight };

    m_uniforms.gpu = device().createCpuVisibleBuffer(&defaultValues, sizeof(UniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    m_uniforms.cpu = reinterpret_cast<UniformData*>(m_uniforms.gpu.map());
    device().setName<VK_OBJECT_TYPE_BUFFER>("terrain_uniforms", m_uniforms.gpu.buffer);
}

void Terrain::initBuffers() {
    VkDrawIndexedIndirectCommand drawIndexedCmd{(3u << (2 * m_options.gpuSubDivisions)), 1, 0, 0, 0};
    VkDrawIndirectCommand drawCmd{1, 1, 0, 0};
    VkDispatchIndirectCommand dispatchCmd{1, 1, 1};

    auto usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    m_emptyBuffer = device().createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int) * 3, "empty_buffer");
    m_drawBuffer = device().createDeviceLocalBuffer(&drawIndexedCmd, sizeof(VkDrawIndexedIndirectCommand), usage);
    m_topViewDrawBuffer = device().createDeviceLocalBuffer(&drawIndexedCmd, sizeof(VkDrawIndirectCommand), usage);
    m_dispatchBuffer = device().createDeviceLocalBuffer(&dispatchCmd, sizeof(VkDispatchIndirectCommand), usage);

    device().setName<VK_OBJECT_TYPE_BUFFER>("terrain_draw_indirect", m_drawBuffer.buffer);
    device().setName<VK_OBJECT_TYPE_BUFFER>("terrain_top_view_draw_indirect", m_topViewDrawBuffer.buffer);
    device().setName<VK_OBJECT_TYPE_BUFFER>("terrain_dispatch_indirect", m_dispatchBuffer.buffer);

    auto cbt = cbt::Tree{ static_cast<int64_t>(m_maxDepth), 1};
    auto heap = cbt.getHeap();
    m_concurrentBinaryTree = device().createDeviceLocalBuffer(heap.data(), heap.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    device().setName<VK_OBJECT_TYPE_BUFFER>("terrain_cbt_heap", m_concurrentBinaryTree.buffer);

    CbtData cbtData{};
    m_cbtInfo.gpu = device().createCpuVisibleBuffer(&cbtData, sizeof(CbtData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    m_cbtInfo.cpu = reinterpret_cast<CbtData*>(m_cbtInfo.gpu.map());
    device().setName<VK_OBJECT_TYPE_BUFFER>("terrain_cbt_info", m_cbtInfo.gpu.buffer);
}

void Terrain::initVertexBuffer() {
    const auto gpuSubd = static_cast<uint64_t>(m_options.gpuSubDivisions);
    std::vector<uint16_t> cIndexes;
    std::vector<glm::vec2> cVertices;
    std::map<uint32_t, uint16_t> hashMap;
    int lebDepth = 2 * gpuSubd;
    int triangleCount = 1 << lebDepth;
    int edgeTessellationFactor = 1 << gpuSubd;

    // compute index and vertex buffer
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

    m_vertices = device().createDeviceLocalBuffer(cVertices.data(), BYTE_SIZE(cVertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    m_indexes = device().createDeviceLocalBuffer(cIndexes.data(), BYTE_SIZE(cIndexes), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    device().setName<VK_OBJECT_TYPE_BUFFER>("terrain_vertex_buffer", m_vertices.buffer);
    device().setName<VK_OBJECT_TYPE_BUFFER>("terrain_index_buffer", m_indexes.buffer);
}

void Terrain::createRenderPipelines() {
    const auto w = m_context->screenWidth;
    const auto h = m_context->screenHeight;

    m_gbuffer.pipeline =
        graphicsPipelineBuilder()
            .shaderStage()
                .vertexShader(FileManager::resource("terrain_render.vert.spv"))
                    .addSpecialization(0u, 0)
                    .addSpecialization(256u, 1)
                    .addSpecialization(should_displace, 2)
                    .addSpecialization(0u, 3)
                    .addSpecialization(0u, 4)
                .fragmentShader(FileManager::resource("terrain_gbuffer.frag.spv"))
            .vertexInputState().clear()
                .addVertexBindingDescription(0, sizeof(glm::vec2), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32_SFLOAT, 0)
            .rasterizationState()
                .cullNone()
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .depthAttachment(VK_FORMAT_D16_UNORM)
            .colorBlendState()
                .attachments(3)
            .layout().clear()
                .addDescriptorSetLayout(m_descriptorSetLayout)
                .addDescriptorSetLayout(bindlessDescriptorSetLayout())
            .name("terrain_gbuffer")
        .build(m_gbuffer.layout);

    auto renderBrunetonBuilder = graphicsPipelineBuilder();
    m_renderBruneton.pipeline =
            renderBrunetonBuilder
            .shaderStage()
                .vertexShader(FileManager::resource("terrain_render.vert.spv"))
                    .addSpecialization(0u, 0)
                    .addSpecialization(256u, 1)
                    .addSpecialization(should_displace, 2)
                    .addSpecialization(0u, 3)
                    .addSpecialization(0u, 4)
                .fragmentShader(FileManager::resource("terrain_render_bruneton.frag.spv"))
            .vertexInputState().clear()
                .addVertexBindingDescription(0, sizeof(glm::vec2), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32_SFLOAT, 0)
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .depthAttachment(VK_FORMAT_D16_UNORM)
            .colorBlendState()
                .attachments(2)
            .layout().clear()
                .addDescriptorSetLayout(m_descriptorSetLayout)
                .addDescriptorSetLayout(bindlessDescriptorSetLayout())
                .addDescriptorSetLayout(AppContext::atmosphere().descriptor.uboDescriptorSetLayout)
                .addDescriptorSetLayout(AppContext::atmosphere().descriptor.lutDescriptorSetLayout)
            .name("terrain_render_bruneton")
        .build(m_renderBruneton.layout);

    m_renderBrunetonWire.pipeline =
            renderBrunetonBuilder
            .shaderStage()
                .geometryShader(FileManager::resource("terrain_render.geom.spv"))
                .fragmentShader(FileManager::resource("terrain_render_wire.frag.spv"))
            .name("terrain_render_bruneton_wire")
        .build(m_renderBrunetonWire.layout);

    auto renderBuilder = graphicsPipelineBuilder();
    m_render.pipeline =
        renderBuilder
            .shaderStage()
                .vertexShader(FileManager::resource("terrain_render.vert.spv"))
                    .addSpecialization(0u, 0)
                    .addSpecialization(256u, 1)
                    .addSpecialization(should_displace, 2)
                    .addSpecialization(0u, 3)
                    .addSpecialization(0u, 4)
                .fragmentShader(FileManager::resource("terrain_render.frag.spv"))
            .vertexInputState().clear()
                .addVertexBindingDescription(0, sizeof(glm::vec2), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32_SFLOAT, 0)
            .rasterizationState()
            .layout().clear()
                .addDescriptorSetLayout(m_descriptorSetLayout)
                .addDescriptorSetLayout(bindlessDescriptorSetLayout())
                .addDescriptorSetLayout(m_atmosphereDescriptor.setLayout)
            .name("terrain_render")
        .build(m_render.layout);

    m_renderWire.pipeline =
        renderBuilder
            .shaderStage()
                .geometryShader(FileManager::resource("terrain_render.geom.spv"))
                .fragmentShader(FileManager::resource("terrain_render_wire.frag.spv"))
            .name("terrain_render_wire")
        .build(m_renderWire.layout);

    m_topView.pipeline =
        graphicsPipelineBuilder()
            .shaderStage()
                .vertexShader(FileManager::resource("empty.vert.spv"))
                .tessellationControlShader(FileManager::resource("terrain_top_view.tesc.spv"))
                .tessellationEvaluationShader(FileManager::resource("terrain_top_view.tese.spv"))
                .geometryShader(FileManager::resource("terrain_top_view.geom.spv"))
                .fragmentShader(FileManager::resource("terrain_top_view.frag.spv"))
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
                .addDescriptorSetLayout(bindlessDescriptorSetLayout())
            .name("terrain_top_view")
        .build(m_topView.layout);
}

Context &Terrain::context() {
    return *m_context;
}

float Terrain::computeLodFactor() {
    const auto h = m_context->screenHeight;
    const auto gpuSubd = m_options.gpuSubDivisions;

    float tmp = 2.0f * tan(glm::radians(camera().fov) / 2.0f) / h * (1 << gpuSubd) * m_options.primitivePixelLengthTarget;
    auto rtVal =  -2.0f * std::log2(tmp) + 2.0f;
    spdlog::debug("[terrain] lod factor {}", rtVal);
    return rtVal;
}

void Terrain::endFrame() {

}

void Terrain::createDescriptorSetLayout() {
    m_descriptorSetLayout =
        device().descriptorSetLayoutBuilder()
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
            .binding(5)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
}

void Terrain::updateDescriptorSets() {
    m_descriptorSet = descriptorPool().allocate({ m_descriptorSetLayout }).front();

    auto writes = initializers::writeDescriptorSets<6>();

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

    writes[5].dstSet = m_descriptorSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[5].descriptorCount = 1;
    VkDescriptorBufferInfo uniformInfo{ m_uniforms.gpu, 0, VK_WHOLE_SIZE };
    writes[5].pBufferInfo = &uniformInfo;

    device().updateDescriptorSets(writes);
    
    m_sets[0] = m_descriptorSet;
}

void Terrain::createComputePipelines() {
    m_compute = ComputePipelines{ m_context->device, metadata() };
    m_compute.createPipelines();
}

std::vector<PipelineMetaData> Terrain::metadata() {
    std::vector<VulkanDescriptorSetLayout*> layouts{ &m_descriptorSetLayout, &bindlessDescriptorSetLayout() };
    return {
        {
            .name = "terrain_leb_dispatcher",
            .shadePath = FileManager::resource("leb_dispatcher.comp.spv"),
            .layouts = layouts,
            .ranges = {},
            .specializationConstants = specializationConstants
        },
        {
            .name = "terrain_cbt_dispatcher",
            .shadePath = FileManager::resource("cbt_dispatcher.comp.spv"),
            .layouts = layouts,
            .ranges = {},
            .specializationConstants = specializationConstants
        },
        {
            .name = "terrain_cbt_sum_reduce_prepass",
            .shadePath = FileManager::resource("cbt_sum_reduce_prepass.comp.spv"),
            .layouts = layouts,
            .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int)} },
            .specializationConstants = specializationConstants
        },
        {
            .name = "terrain_cbt_sum_reduce",
            .shadePath = FileManager::resource("cbt_sum_reduce.comp.spv"),
            .layouts = layouts,
            .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int)} },
            .specializationConstants = specializationConstants
        },
        {
            .name = "terrain_subdivide",
            .shadePath = FileManager::resource("terrain_subdivide.comp.spv"),
            .layouts = layouts,
            .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int)} },
            .specializationConstants = specializationConstants
        },
        {
            .name = "terrain_cbt_info",
            .shadePath = FileManager::resource("cbt_info.comp.spv"),
            .layouts = layouts,
            .specializationConstants = specializationConstants
        },
    };
}

void Terrain::cbtDispatch(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("terrain_cbt_dispatcher"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("terrain_cbt_dispatcher"), 0, COUNT(m_sets), m_sets.data(), 0,nullptr);
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    Barriers::pushAndFlush(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
}

void Terrain::lebSubdivision(VkCommandBuffer commandBuffer, int pingPong) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("terrain_subdivide"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("terrain_subdivide"), 0, COUNT(m_sets), m_sets.data(), 0,nullptr);
    vkCmdPushConstants(commandBuffer, m_compute.layout("terrain_subdivide"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &pingPong);
    vkCmdDispatchIndirect(commandBuffer, m_dispatchBuffer, 0);
    Barrier::computeWriteToRead(commandBuffer);
}

void Terrain::sumReducePrePass(VkCommandBuffer commandBuffer) {
    auto itr = m_maxDepth;
    auto cnt = ((1 << itr) >> 5);
    auto numGroup = (cnt >= 256) ? (cnt >> 8) : 1;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("terrain_cbt_sum_reduce_prepass"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("terrain_cbt_sum_reduce_prepass"), 0, COUNT(m_sets), m_sets.data(), 0,nullptr);
    vkCmdPushConstants(commandBuffer, m_compute.layout("terrain_cbt_sum_reduce_prepass"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &itr);
    vkCmdDispatch(commandBuffer, numGroup, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);

}

void Terrain::sumReduceCbt(VkCommandBuffer commandBuffer) {

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
}

void Terrain::lebDispatch(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("terrain_leb_dispatcher"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("terrain_leb_dispatcher"), 0, COUNT(m_sets), m_sets.data(), 0,nullptr);
    vkCmdDispatch(commandBuffer, 1, 1, 1);

    static auto dstStageMask = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    static auto dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    Barriers::pushAndFlush(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, dstStageMask , VK_ACCESS_SHADER_WRITE_BIT, dstAccessMask);
}

void Terrain::getCbtInfo(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("terrain_cbt_info"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("terrain_cbt_info"), 0, COUNT(m_sets), m_sets.data(), 0,nullptr);
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    Barrier::computeWriteToFragmentRead(commandBuffer);
}

uint Terrain::nodeCount() const {
    return m_cbtInfo.cpu->nodeCount;
}

void Terrain::wireOn() {
    m_options.wire = true;
}

void Terrain::wireOff() {
    m_options.wire = false;
}

void Terrain::topViewOn() {
    m_options.topView = true;
}

void Terrain::topViewOff() {
    m_options.topView = true;
}

void Terrain::controls() {
    ImGui::Begin("terrain");
    ImGui::SetWindowSize({});

    ImGui::SliderFloat("Pixels/Edge", &m_options.primitivePixelLengthTarget, 1, 32);
    ImGui::SliderFloat("Dmap scale", &m_options.dmapScale, 0, 1);
    ImGui::SliderFloat("Lod Std", &m_options.minLodStdev, 0, 1);

    ImGui::Checkbox("Wire", &m_options.wire);
    ImGui::SameLine();
    ImGui::Checkbox("topView", &m_options.topView);

    ImGui::End();
}

TerrainInfo Terrain::getInfo() const {
    return { m_dmap.width, m_dmap.height, m_dmap.zMin, m_dmap.zMax };
}
