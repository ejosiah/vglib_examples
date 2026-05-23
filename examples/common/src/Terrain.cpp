#include "vista/Terrain.hpp"
#include "Barrier.hpp"

#include <glm/glm.hpp>
#include <imgui.h>
#include "AppContext.hpp"
#include <algorithm>

Terrain::Terrain(Context &context, AtmosphereModel::Descriptor atmDescriptor, glm::ivec2 terrainSize, glm::vec2 heightScale)
: SubdivisionGrid(*context.device, *context.descriptorPool, *context.bindlessDescriptor,
                  "terrain", {context.screenWidth, context.screenHeight}, 1, context.profiler)
, m_context{&context}
, m_dmap{.terrainSize = terrainSize, .heightScale = {std::min(heightScale.x, heightScale.y), std::max(heightScale.x, heightScale.y)}}
, m_atmosphereDescriptor(atmDescriptor)
{
    m_normals.constants.boundsMin = {-float(terrainSize.x) * 0.5f, m_dmap.heightScale.x, -float(terrainSize.y) * 0.5f};

    static uint WorkGroupSize = 256;
    static uint cbtID = 0;
    static uint projection_method = 0;
    static uint should_cull_triangle = 1;
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
    initQueryStats();
    initUniforms();
    initNormalData();
    loadTerrainTextures();
    SubdivisionGrid::init();
    createRenderPipelines();
    m_compute.add({
      .name = "histogram",
      .shadePath = resource("vista_normal_histogram.comp.spv"),
      .layouts = {m_layouts[0], m_layouts[1], &m_descriptorSetLayout, &m_triangleDescriptorSetLayout, &m_normals.descriptorSetLayout},
      .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_normals.constants)} },
      .specializationConstants = specializationConstants});
    m_compute.createPipelines();
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
    m_uniforms.cpu->mouse = context().mouse;
    m_uniforms.cpu->resolution = { m_context->screenWidth, m_context->screenHeight };
    m_uniforms.cpu->tileSize = glm::vec2{m_options.tileSize};
    m_uniforms.cpu->showTiles = uint(m_options.showTiles);
    m_uniforms.cpu->tileColor = uint(m_options.tileColor);
    m_uniforms.cpu->wireframeOn = uint(m_options.wire);
    m_uniforms.cpu->useTriplanerMapping = uint(m_options.triplanerMapping);
    m_uniforms.cpu->useLeadrLighting = uint(m_options.useLeadrLighting);
    m_uniforms.cpu->visualizeDepthFade = uint(m_options.visualizeDepthFade);
    m_uniforms.cpu->blendMin = m_options.blendMin;
    m_uniforms.cpu->blendMax = m_options.blendMax;
    m_uniforms.cpu->shadowDarkness = m_options.shadowDarkness;
    m_uniforms.cpu->visualizeWaterFlow = uint(m_options.visualizeWaterFlow);
    m_uniforms.cpu->waterFlowTextureIndex = m_options.waterFlowTextureIndex;
    m_uniforms.cpu->waterFlowScale = m_options.waterFlowScale;
    m_uniforms.cpu->visualizeGraphSignal = uint(m_options.visualizeGraphSignal);
    m_uniforms.cpu->graphSignalAxis = m_options.graphSignalAxis;
    m_uniforms.cpu->graphSignalPosition = m_options.graphSignalPosition;
    m_uniforms.cpu->graphSignalWidth = m_options.graphSignalWidth;
    m_uniforms.cpu->graphSignalColor = m_options.graphSignalColor;
    static Frustum frustum;
    Frustum::extractFrustum(frustum, mvp);
    std::memcpy(m_uniforms.cpu->frustumPlanes.data(), frustum.cp.data(), BYTE_SIZE(frustum.cp));
}

void Terrain::preProcess(VkCommandBuffer commandBuffer) {
    m_queryPool.reset(commandBuffer, "triangle_count");
    update(commandBuffer);
    generateNormals(commandBuffer);
}


void Terrain::render(VkCommandBuffer commandBuffer) {
    profiler().profile(m_queryIds[QUERY_RENDER_ID], commandBuffer, [&]{
        static std::array<VkDescriptorSet, 4> sets;
        sets[0] = m_sets[0];
        sets[1] = m_sets[1];
        sets[2] = m_sets[2];
        sets[3] = m_atmosphereDescriptor.set;

        VkDeviceSize offset = 0;
        m_queryPool.query(commandBuffer, "triangle_count", [&]{
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.pipeline.handle);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_render.layout.handle, 0, COUNT(sets), sets.data(), 0,nullptr);
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertices.buffer, &offset);
            vkCmdBindIndexBuffer(commandBuffer, m_indexes, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexedIndirect(commandBuffer, m_drawBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
        });
    });
}

void Terrain::renderTopView(VkCommandBuffer commandBuffer) {
    if(!m_options.topView) return;
    topView(commandBuffer);
}

void Terrain::inspect(VkCommandBuffer commandBuffer) {
    if(!m_options.inspect) return;

    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = m_sets[0];
    sets[1] = m_sets[1];
    sets[2] = m_sets[2];
    sets[3] = m_atmosphereDescriptor.set;

    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_inspect.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_inspect.layout.handle, 0, COUNT(sets), sets.data(), 0,nullptr);
    vkCmdPushConstants(commandBuffer, m_inspect.layout.handle, VK_SHADER_STAGE_GEOMETRY_BIT, 0, sizeof(m_inspectConstants), &m_inspectConstants);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertices.buffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, m_indexes, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexedIndirect(commandBuffer, m_drawBuffer, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
}

void Terrain::initUniforms() {
    defaultValues.damp_tex_index = context().dmap_tex_index;
    defaultValues.dmap_normal_tex_index = context().dmap_normal_tex_index;
    defaultValues.dmap_slope_moments0_tex_index = context().dmap_slope_moments0_tex_index;
    defaultValues.dmap_slope_moments1_tex_index = context().dmap_slope_moments1_tex_index;
    defaultValues.shadow_tex_index = context().dmap_shadow_tex_index;
    defaultValues.sunSize = AppContext::atmosphere().info.cpu->sunSize;
    defaultValues.whitePoint = AppContext::atmosphere().info.cpu->whitePoint;
    defaultValues.exposure = AppContext::atmosphere().info.cpu->exposure;

    const glm::vec2 terrainSize = m_dmap.terrainSize;
    const auto heightScale = m_dmap.heightScale;
    const float zMin = heightScale.x;
    const float zMax = heightScale.y;

    glm::mat4 model = glm::mat4{1};
    model = glm::translate(model, {0, -zMin, 0});
    model = glm::scale(model, {terrainSize.x, zMax - zMin, terrainSize.y});
    model = glm::rotate(model, -glm::half_pi<float>(), {1, 0, 0});
    model = glm::translate(model, {-0.5f, -0.5f, 0.0f});
    defaultValues.modelMatrix = model;
    defaultValues.resolution = { m_context->screenWidth, m_context->screenHeight };

    m_uniforms.gpu = device().createCpuVisibleBuffer(&defaultValues, sizeof(UniformData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    m_uniforms.cpu = reinterpret_cast<UniformData*>(m_uniforms.gpu.map());
    device().setName<VK_OBJECT_TYPE_BUFFER>("terrain_uniforms", m_uniforms.gpu.buffer);
}

void Terrain::createRenderPipelines() {
    const auto w = m_context->screenWidth;
    const auto h = m_context->screenHeight;
    const auto scissorWidth = static_cast<int32_t>(w);
    const auto scissorHeight = static_cast<int32_t>(h);

    m_render.pipeline =
        graphicsPipelineBuilder()
            .shaderStage()
                .vertexShader(FileManager::resource("vista_terrain_render.vert.spv"))
                    .addSpecialization(0u, 0)
                    .addSpecialization(256u, 1)
                    .addSpecialization(should_displace, 2)
                    .addSpecialization(0u, 3)
                    .addSpecialization(1u, 4)
                .geometryShader(FileManager::resource("vista_terrain_render.geom.spv"))
                .fragmentShader(FileManager::resource(renderFragmentShaderPath()))
            .vertexInputState().clear()
                .addVertexBindingDescription(0, sizeof(glm::vec2), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32_SFLOAT, 0)
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .depthAttachment(VK_FORMAT_D16_UNORM)
            .viewportState().clear()
                .viewport()
                    .origin(0, 0)
                    .dimension(w, h)
                .scissor()
                    .offset(0, 0)
                    .extent(scissorWidth, scissorHeight)
                .add()
            .dynamicState()
                .viewport()
                .scissor()
            .colorBlendState()
                .attachments(2)
            .layout().clear()
                .addDescriptorSetLayout(m_subdGridDescriptorSetLayout)
                .addDescriptorSetLayout(bindlessDescriptorSetLayout())
                .addDescriptorSetLayout(m_descriptorSetLayout)
                .addDescriptorSetLayout(m_atmosphereDescriptor.setLayout)
            .name("terrain_render")
        .build(m_render.layout);
}

std::string Terrain::renderFragmentShaderPath() const {
    return "vista_terrain_render.frag.spv";
}

bool Terrain::usesMaterialTextures() const {
    return true;
}

Terrain::MaterialTexturePaths Terrain::materialTexturePaths() const {
    return {
        .dirtAlbedo = "GroundDirtRocky015/GroundDirtRocky015_COL_4K.jpg",
        .dirtAo = "GroundDirtRocky015/GroundDirtRocky015_AO_4K.jpg",
        .dirtRoughness = "GroundDirtRocky015/GroundDirtRocky015_GLOSS_4K.jpg",
        .dirtNormal = "GroundDirtRocky015/GroundDirtRocky015_NRM_4K.jpg",
        .grassAlbedo = "GrassShort001/GrassShort001_COL_VAR1_4K.jpg",
        .grassAo = "GrassShort001/GrassShort001_AO_4K.jpg",
        .grassRoughness = "GrassShort001/GrassShort001_GLOSS_4K.jpg",
        .grassNormal = "GrassShort001/GrassShort001_NRM_4K.jpg",
        .noise = "BlueNoiseTextures/1024_1024/LDR_RGBA_0.png",
    };
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
    m_triangleCount = m_queryPool.queryResult<uint>(VK_QUERY_RESULT_WAIT_BIT);
}

void Terrain::createDescriptorSetLayout() {
    SubdivisionGrid::createDescriptorSetLayout();
    m_descriptorSetLayout =
        device().descriptorSetLayoutBuilder()
            .name("terrain_subdivision_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();


    m_normals.descriptorSetLayout =
        device().descriptorSetLayoutBuilder()
            .name("normal_gen_descriptor_set_layout")
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
        .createLayout();
}

void Terrain::updateDescriptorSets() {
    SubdivisionGrid::updateDescriptorSets();
    auto sets = descriptorPool().allocate({ m_descriptorSetLayout, m_normals.descriptorSetLayout });
    m_descriptorSet = sets[0];
    m_normals.descriptorSet = sets[1];

    auto writes = initializers::writeDescriptorSets<4>();

    writes[0].dstSet = m_descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uniformInfo{ m_uniforms.gpu, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &uniformInfo;

    writes[1].dstSet = m_normals.descriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo countsInfo{ m_normals.counts, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &countsInfo;

    writes[2].dstSet = m_normals.descriptorSet;
    writes[2].dstBinding = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo normalsInfo{ m_normals.normals, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &normalsInfo;

    writes[3].dstSet = m_normals.descriptorSet;
    writes[3].dstBinding = 2;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    VkDescriptorBufferInfo countersInfo{ m_normals.counters, 0, VK_WHOLE_SIZE };
    writes[3].pBufferInfo = &countersInfo;


    device().updateDescriptorSets(writes);
    m_sets[2] = m_descriptorSet;
}

void Terrain::createComputePipelines() {
    m_compute = ComputePipelines{ m_context->device, metadata() };
    m_compute.createPipelines();
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

void Terrain::controls(bool show) {
    if(!show) return;

    ImGui::Begin("terrain");
    ImGui::SetWindowSize({});
    controlsContent();
    ImGui::End();
}

void Terrain::controlsContent() {
    ImGui::SliderFloat("Pixels/Edge", &m_options.primitivePixelLengthTarget, 1, 32);
    ImGui::SliderFloat("Dmap scale", &m_options.dmapScale, 0, 1);
    ImGui::SliderFloat("Lod Std", &m_options.minLodStdev, 0, 1);
    ImGui::SliderFloat("Mat blend min", &m_options.blendMin, 0, 1);
    ImGui::SliderFloat("Mat blend max", &m_options.blendMax, 0, 1);
    ImGui::SliderFloat("tile size", &m_options.tileSize, 0.1, 1000);
    ImGui::Checkbox("tri planer mapping", &m_options.triplanerMapping);
    ImGui::SameLine();
    ImGui::Checkbox("Wire", &m_options.wire);
    ImGui::SameLine();
    ImGui::Checkbox("topView", &m_options.topView);

    ImGui::Checkbox("Show tiles", &m_options.showTiles);
    ImGui::Checkbox("Visualize depth fade", &m_options.visualizeDepthFade);
    if(m_options.showTiles) {
        ImGui::RadioButton("uv", &m_options.tileColor, 0); ImGui::SameLine();
        ImGui::RadioButton("checkerboard", &m_options.tileColor, 1); ImGui::SameLine();
        ImGui::RadioButton("random", &m_options.tileColor, 2);
    }
    ImGui::Text("Cbt info:\n\tNode Count: %d\n\tMax depth: %d", m_cbtInfo.cpu->nodeCount, m_cbtInfo.cpu->maxDepth);
    ImGui::Text("Minimum triangle area %f",  *as<float>(&m_uniforms.cpu->minArea));
    ImGui::Text("Triangle count: %d", m_triangleCount);
}

void Terrain::lightingControls() {
    ImGui::Checkbox("LEADR lighting", &m_options.useLeadrLighting);
    ImGui::SliderFloat("Shadow darkness", &m_options.shadowDarkness, 0.0f, 1.0f);
}

void Terrain::setWaterFlowVisualization(bool enabled, uint textureIndex, float scale) {
    m_options.visualizeWaterFlow = enabled;
    m_options.waterFlowTextureIndex = textureIndex;
    m_options.waterFlowScale = scale;

    if(m_uniforms.cpu) {
        m_uniforms.cpu->visualizeWaterFlow = uint(enabled);
        m_uniforms.cpu->waterFlowTextureIndex = textureIndex;
        m_uniforms.cpu->waterFlowScale = scale;
    }
}

void Terrain::setGraphSignalOverlay(bool enabled, int axis, float position, glm::vec3 color, float width) {
    m_options.visualizeGraphSignal = enabled;
    m_options.graphSignalAxis = static_cast<uint>(std::clamp(axis, 0, 1));
    m_options.graphSignalPosition = std::clamp(position, 0.0f, 1.0f);
    m_options.graphSignalWidth = std::max(width, 0.0f);
    m_options.graphSignalColor = color;

    if(m_uniforms.cpu) {
        m_uniforms.cpu->visualizeGraphSignal = uint(enabled);
        m_uniforms.cpu->graphSignalAxis = m_options.graphSignalAxis;
        m_uniforms.cpu->graphSignalPosition = m_options.graphSignalPosition;
        m_uniforms.cpu->graphSignalWidth = m_options.graphSignalWidth;
        m_uniforms.cpu->graphSignalColor = m_options.graphSignalColor;
    }
}

TerrainInfo Terrain::getInfo() const {
    return { m_dmap.terrainSize, m_dmap.heightScale };
}

float Terrain::displacementScale() const {
    return m_options.dmapScale;
}

float Terrain::printPerfStats() {
    const auto toMillis = 1e-6f;
    auto total = 0.0f;

    if (ImGui::TreeNode("Terrain")) {
        ImGuiTreeNodeFlags leaf = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        for(auto name : m_queryIds) {
            auto duration = profiler().queries[name].movingAverage.value * toMillis;
            ImGui::TreeNodeEx(name.c_str(), leaf, "%s: %f ms", name.c_str(), duration);
            total += duration;
        }
        ImGui::TreeNodeEx("total", leaf, "total: %f ms", total);
        ImGui::TreePop();
    }
    return total;
}

void Terrain::loadTerrainTextures() {
    if(!usesMaterialTextures()) {
        return;
    }

    const auto levels = 11u;
    const auto paths = materialTexturePaths();
    textures::fromFile(device(), dirt.albedoMap, resource(paths.dirtAlbedo), false, VK_FORMAT_R8G8B8A8_SRGB, levels, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    textures::fromFile(device(), dirt.aoMap, resource(paths.dirtAo), false, VK_FORMAT_R8G8B8A8_UNORM, levels, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    textures::fromFile(device(), dirt.roughnessMap, resource(paths.dirtRoughness), false, VK_FORMAT_R8G8B8A8_UNORM, levels, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    textures::fromFile(device(), dirt.normalMap, resource(paths.dirtNormal), false, VK_FORMAT_R8G8B8A8_UNORM, levels, VK_SAMPLER_ADDRESS_MODE_REPEAT);

    textures::fromFile(device(), grass.albedoMap, resource(paths.grassAlbedo), false, VK_FORMAT_R8G8B8A8_SRGB, levels, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    textures::fromFile(device(), grass.aoMap, resource(paths.grassAo), false, VK_FORMAT_R8G8B8A8_UNORM, levels, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    textures::fromFile(device(), grass.roughnessMap, resource(paths.grassRoughness), false, VK_FORMAT_R8G8B8A8_UNORM, levels, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    textures::fromFile(device(), grass.normalMap, resource(paths.grassNormal), false, VK_FORMAT_R8G8B8A8_UNORM, levels, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    textures::fromFile(device(), m_noise, resource(paths.noise), false, VK_FORMAT_R8G8B8A8_UNORM, levels, VK_SAMPLER_ADDRESS_MODE_REPEAT);


    textures::generateLOD(device(), dirt.albedoMap, levels);
    textures::generateLOD(device(), dirt.aoMap, levels);
    textures::generateLOD(device(), dirt.roughnessMap, levels);
    textures::generateLOD(device(), dirt.normalMap, levels);

    textures::generateLOD(device(), grass.albedoMap, levels);
    textures::generateLOD(device(), grass.aoMap, levels);
    textures::generateLOD(device(), grass.roughnessMap, levels);
    textures::generateLOD(device(), grass.normalMap, levels);

    m_uniforms.cpu->noiseTextureIndex = bindlessDescriptor().update(m_noise, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    m_uniforms.cpu->dirtyAlbedoMapIndex = bindlessDescriptor().update(dirt.albedoMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    m_uniforms.cpu->dirtyAoMapIndex = bindlessDescriptor().update(dirt.aoMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    m_uniforms.cpu->dirtyRoughnessMapIndex = bindlessDescriptor().update(dirt.roughnessMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    m_uniforms.cpu->dirtyNormalMapIndex = bindlessDescriptor().update(dirt.normalMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    m_uniforms.cpu->grassAlbedoMapIndex = bindlessDescriptor().update(grass.albedoMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    m_uniforms.cpu->grassAoMapIndex = bindlessDescriptor().update(grass.aoMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    m_uniforms.cpu->grassRoughnessMapIndex = bindlessDescriptor().update(grass.roughnessMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    m_uniforms.cpu->grassNormalMapIndex = bindlessDescriptor().update(grass.normalMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
}

void Terrain::checkAppInput() {
    static bool initialPress = true;

    if(m_options.inspect && mouseInput().right.held && initialPress) {
        m_inspectConstants.state = 1;
        m_inspectConstants.start = mouseInput().position;
    }

    if(m_options.inspect && mouseInput().right.released) {
        m_inspectConstants.end = mouseInput().position;
        m_inspectConstants.state = 0;
    }
}

PipelineMetaData Terrain::subdivisionMetadata() {
    auto layouts = m_layouts;
    layouts.push_back(&m_descriptorSetLayout);
    return {
            .name = "terrain_subdivide",
            .shadePath = FileManager::resource("vista_terrain_subdivide.comp.spv"),
            .layouts = layouts,
            .ranges = {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int)}},
            .specializationConstants = specializationConstants

    };
}

void Terrain::subdivide(VkCommandBuffer commandBuffer, int pingPong) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("terrain_subdivide"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("terrain_subdivide"), 0, COUNT(m_sets), m_sets.data(), 0,nullptr);
    vkCmdPushConstants(commandBuffer, m_compute.layout("terrain_subdivide"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &pingPong);
    vkCmdDispatchIndirect(commandBuffer, m_dispatchBuffer, 0);
    Barrier::computeWriteToRead(commandBuffer);
}

void Terrain::initNormalData() {
    const auto tableSize = m_normals.constants.tableSize;
    m_normals.counts = device().createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, tableSize * sizeof(uint), "terrain_normal_counts");
    m_normals.normals = device().createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, tableSize * sizeof(glm::vec4), "terrain_normals");
    m_normals.counters = device().createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(uint), "terrain_normals_counters");

    m_prefixSum = PrefixSum{ &device() };
    m_prefixSum.init();
}

void Terrain::initQueryStats() {
    m_queryPool = PipelineStatsQueryPool{device(), 1u, VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT};
    m_queryPool.addQuery("triangle_count");
}

void Terrain::generateNormals(VkCommandBuffer commandBuffer) {
    computeHistogram(commandBuffer);
//    computePartialSum(commandBuffer);
    reorder(commandBuffer);
}

void Terrain::computeHistogram(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 5> sets;
    sets[0] = m_sets[0];
    sets[1] = m_sets[1];
    sets[2] = m_sets[2];
    sets[3] = m_triangleDescriptorSet;
    sets[4] = m_normals.descriptorSet;

    vkCmdFillBuffer(commandBuffer, m_normals.counters.buffer, 0, m_normals.counters.size, 0u);
    Barrier::transferWriteToComputeRead(commandBuffer);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("histogram"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("histogram"), 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, m_compute.layout("histogram"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_normals.constants), &m_normals.constants);
    vkCmdDispatchIndirect(commandBuffer, m_dispatchBuffer, 0);
}

void Terrain::computePartialSum(VkCommandBuffer commandBuffer) {
    m_prefixSum.inclusive(commandBuffer, m_normals.counts, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

void Terrain::reorder(VkCommandBuffer commandBuffer) {

}
