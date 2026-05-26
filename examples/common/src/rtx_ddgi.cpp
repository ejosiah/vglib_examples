#include "rtx/ddgi.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "filemanager.hpp"
#include "Texture.h"
#include "gltf/Bvh.hpp"
#include "Barrier.hpp"

rtx::ddgi::ddgi(const ddgi::Params &p)
        : m_device{ &p.device }
        , m_bindlessDescriptor{ &p.bindlessDescriptor }
        , m_descriptorPool{ &p.descriptorPool }
        , m_cameraInfo{ p.cameraInfo }
        , m_lightDescriptorSetLayout{ p.lightDescriptorSetLayout }
        , m_bvhDescriptorSetLayoutLayout{ p.bvhDescriptorSetLayoutLayout }
        , m_lightsDescriptorSet{ p.lightDescriptorSet }
        , m_bvhDescriptorSet{ p.bvhDescriptorSet }
        , m_constants{
             .probe_grid_position = { p.sceneHalfWidth.x - 2.0f * p.sceneHalfWidth.x, 0.5, p.sceneHalfWidth.z - 2.0f * p.sceneHalfWidth.z },
             .depth_texture_index = p.depthBufferIndex, .normal_texture_index = p.normalBufferIndex,
            .num_lights = p.numLights }
{
    assert(p.numLights > 0);
}

void rtx::ddgi::init() {
    initTextures();
    createBuffers();
    createDescriptorSetLayouts();
    updateDescriptorSet();
    createProbeRtPipeline();
    initComputePipelines();
}

void rtx::ddgi::createProbeRtPipeline() {
    auto rayGenShaderModule = m_device->createShaderModule( FileManager::resource("rtx_ddgi_probe.rgen.spv"));
    auto missShaderModule = m_device->createShaderModule( FileManager::resource("rtx_ddgi_probe.rmiss.spv"));
    auto chitShaderModule = m_device->createShaderModule( FileManager::resource("rtx_ddgi_probe.rchit.spv"));
    
    auto shaders = std::vector<ShaderInfo>(shader_type::Count);
    shaders[shader_type::RayGen] = { rayGenShaderModule, VK_SHADER_STAGE_RAYGEN_BIT_KHR};
    shaders[shader_type::Miss] = { missShaderModule, VK_SHADER_STAGE_MISS_BIT_KHR};
    shaders[shader_type::ClosesHit] = { chitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
    shaderGroups.push_back(m_probeRT.shaderTablesDesc.rayGenGroup());
    shaderGroups.push_back(m_probeRT.shaderTablesDesc.addMissGroup(shader_type::Miss));
    shaderGroups.push_back(m_probeRT.shaderTablesDesc.addHitGroup(shader_type::ClosesHit));

    auto stages = map_range(shaders, [](auto& shader){
        return VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = shader.stage,
                .module = shader.module.handle,
                .pName = shader.entry,
        };
    });

    m_probeRT.layout = m_device->createPipelineLayout({
        m_constantsDescriptorSetLayout,
        *m_bindlessDescriptor->ncDescriptorSetLayout(),
        gltf::bvh::Bvh::rtxDescriptorSetLayout,
        m_lightDescriptorSetLayout,
        *m_cameraInfo->descriptorSetLayout()
    });
    VkRayTracingPipelineCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
    createInfo.stageCount = COUNT(stages);
    createInfo.pStages = stages.data();
    createInfo.groupCount = COUNT(shaderGroups);
    createInfo.pGroups = shaderGroups.data();
    createInfo.maxPipelineRayRecursionDepth = 1;
    createInfo.layout = m_probeRT.layout.handle;

    m_probeRT.pipeline = m_device->createRayTracingPipeline(createInfo);
    m_probeRT.bindingTables = m_probeRT.shaderTablesDesc.compile(*m_device, m_probeRT.pipeline);
}

Probes rtx::ddgi::probes() const {
    return m_probes;
}

void rtx::ddgi::exec(VkCommandBuffer commandBuffer) {
    m_device->section([&]{
        probeRT(commandBuffer);
        updateIrradiance(commandBuffer);
        updateVisibility(commandBuffer);
        sampleIndirect(commandBuffer);
    }, commandBuffer, "ddgi");
}

void rtx::ddgi::probeRT(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 5> sets;
    sets[0] = m_constantsDescriptorSet;
    sets[1] = m_bindlessDescriptor->descriptorSet;
    sets[2] = m_bvhDescriptorSet;
    sets[3] = m_lightsDescriptorSet;
    sets[4] = *m_cameraInfo->descriptorSet();

    vkCmdUpdateBuffer(commandBuffer, m_constantsBuffer, 0, sizeof(m_constants), &m_constants);
    Barrier::transferWriteToComputeRead(commandBuffer);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_probeRT.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_probeRT.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdTraceRaysKHR(commandBuffer, m_probeRT.bindingTables.rayGen, m_probeRT.bindingTables.miss, m_probeRT.bindingTables.closestHit,
                      m_probeRT.bindingTables.callable, m_raysPerProbe, m_numProbes, 1);

    Barrier::computeWriteToRead(commandBuffer);
}

void rtx::ddgi::updateIrradiance(VkCommandBuffer commandBuffer) {
    const auto gx = static_cast<uint32_t>(m_irradianceAtlasWidth + 7) / 8u;
    const auto gy = static_cast<uint32_t>(m_irradianceAtlasHeight + 7) / 8u;

    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = m_constantsDescriptorSet;
    sets[1] = m_bindlessDescriptor->descriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("irradiance_update"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("irradiance_update"), 0, COUNT(sets), sets.data(), 0, 0);
    vkCmdDispatch(commandBuffer, gx, gy, 1u);
}

void rtx::ddgi::updateVisibility(VkCommandBuffer commandBuffer) {
    const auto gx = static_cast<uint32_t>(m_irradianceAtlasWidth + 7) / 8u;
    const auto gy = static_cast<uint32_t>(m_irradianceAtlasHeight + 7) / 8u;

    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = m_constantsDescriptorSet;
    sets[1] = m_bindlessDescriptor->descriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("visibility_update"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("visibility_update"), 0, COUNT(sets), sets.data(), 0, 0);
    vkCmdDispatch(commandBuffer, gx, gy, 1u);
    Barrier::computeWriteToRead(commandBuffer);
}

void rtx::ddgi::sampleIndirect(VkCommandBuffer commandBuffer) {
    const auto res = getResolution();
    const auto gx = static_cast<uint32_t>(res.x + 7) / 8u;
    const auto gy = static_cast<uint32_t>(res.y + 7) / 8u;

    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = m_constantsDescriptorSet;
    sets[1] = m_bindlessDescriptor->descriptorSet;
    sets[2] = *m_cameraInfo->descriptorSet();

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("sample_indirect_light"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("sample_indirect_light"), 0, COUNT(sets), sets.data(), 0, 0);
    vkCmdDispatch(commandBuffer, gx, gy, 1u);
    Barrier::computeWriteToFragmentRead(commandBuffer);
}

void rtx::ddgi::createBuffers() {
    m_constantsBuffer = m_device->createDeviceLocalBuffer(&m_constants, sizeof(m_constants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    std::vector probeStatus(m_probes.count.x * m_probes.count.y * m_probes.count.z, 0);
    m_probeStatus = m_device->createDeviceLocalBuffer(probeStatus.data(), BYTE_SIZE(probeStatus), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

void rtx::ddgi::initTextures() {
    const auto w = int(m_cameraInfo->cpu().viewportSize.x);
    const auto h = int(m_cameraInfo->cpu().viewportSize.y);
    const auto w2 =  w/(halfResolution() ? 2 : 1);
    const auto h2 = h/(halfResolution() ? 2 : 1);
    textures::create(*m_device, m_indirectLight, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {w2, h2, 1});

    const auto numProbes  = m_probes.count.x * m_probes.count.y * m_probes.count.z;
    const auto numRays = m_constants.probe_rays;
    textures::create(*m_device, m_radiance, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {numRays, numProbes, 1});

    const auto octahedralIrradianceSize =  m_constants.irradiance_side_length + 2;
    m_irradianceAtlasWidth = ( octahedralIrradianceSize * m_probes.count.x * m_probes.count.y );
    m_irradianceAtlasHeight = ( octahedralIrradianceSize * m_probes.count.z );
    textures::create(*m_device, m_probeGridIrradiance, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {m_irradianceAtlasWidth, m_irradianceAtlasHeight, 1});

    const auto octahedralVisibilitySize =  m_constants.visibility_side_length + 2;
    m_visibilityAtlasWidth = ( octahedralVisibilitySize * m_probes.count.x * m_probes.count.y );
    m_visibilityAtlasHeight = ( octahedralVisibilitySize * m_probes.count.z );
    textures::create(*m_device, m_probeGridVisibility, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16_SFLOAT, {m_visibilityAtlasWidth, m_visibilityAtlasHeight, 1});

    textures::create(*m_device, m_probeOffset, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {m_probes.count.x * m_probes.count.y , m_probes.count.z, 1});

    m_indirectLight.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    m_radiance.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    m_probeGridIrradiance.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    m_probeGridVisibility.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    m_probeOffset.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    // image bindings
    m_constants.radiance_image_index = m_bindlessDescriptor->update(m_radiance, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
    m_constants.indirect_image_index = m_bindlessDescriptor->update(m_indirectLight, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);

    // texture bindings
    m_constants.indirect_texture_index = m_bindlessDescriptor->update(m_indirectLight, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_LAYOUT_GENERAL);
    m_constants.radiance_texture_index = m_bindlessDescriptor->update(m_radiance, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_LAYOUT_GENERAL);
    m_constants.irradiance_texture_index = m_bindlessDescriptor->update(m_probeGridIrradiance, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_LAYOUT_GENERAL);
    m_constants.visibility_texture_index = m_bindlessDescriptor->update(m_probeGridVisibility, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_LAYOUT_GENERAL);
}

void rtx::ddgi::createDescriptorSetLayouts() {
    m_constantsDescriptorSetLayout =
        m_device->descriptorSetLayoutBuilder()
            .name("ddgi_constants_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();
}

void rtx::ddgi::updateDescriptorSet() {
    auto sets = m_descriptorPool->allocate({ m_constantsDescriptorSetLayout });
    m_constantsDescriptorSet = sets[0];

    auto writes = initializers::writeDescriptorSets<4>();

    writes[0].dstSet = m_constantsDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uniformInfo{m_constantsBuffer, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &uniformInfo;

    writes[1].dstSet = m_constantsDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo probeStatusInfo{m_probeStatus, 0, VK_WHOLE_SIZE};
    writes[1].pBufferInfo = &probeStatusInfo;

    writes[2].dstSet = m_constantsDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo irradianceInfo{VK_NULL_HANDLE, m_probeGridIrradiance.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[2].pImageInfo = &irradianceInfo;

    writes[3].dstSet = m_constantsDescriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo visibilityInfo{VK_NULL_HANDLE, m_probeGridVisibility.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[3].pImageInfo = &visibilityInfo;


    m_device->updateDescriptorSets(writes);
}

std::vector<PipelineMetaData> rtx::ddgi::pipelines() {
    return {
            {
                .name = "irradiance_update",
                .shadePath = FileManager::resource("rtx_ddgi_irradiance_update.comp.spv"),
                .layouts = {
                    &m_constantsDescriptorSetLayout,
                    m_bindlessDescriptor->ncDescriptorSetLayout()
                },
            },
            {
                .name = "visibility_update",
                .shadePath = FileManager::resource("rtx_ddgi_visibility_update.comp.spv"),
                .layouts = {
                    &m_constantsDescriptorSetLayout,
                    m_bindlessDescriptor->ncDescriptorSetLayout()
                },
            },
            {
                .name = "sample_indirect_light",
                .shadePath = FileManager::resource("rtx_ddgi_sample_indirect_light.comp.spv"),
                .layouts = {
                    &m_constantsDescriptorSetLayout,
                    m_bindlessDescriptor->ncDescriptorSetLayout(),
                    m_cameraInfo->descriptorSetLayout()
                },
            },
    };
}

void rtx::ddgi::newFrame() {
    auto offset = m_constants.probe_counts/2  - m_constants.probe_counts;

    m_constants.probe_grid_position = { offset.x, 0.5, offset.z};
    m_constants.reciprocal_probe_spacing = 1.f/m_constants.probe_spacing;
    m_raysPerProbe = m_constants.probe_rays;
    m_numProbes = m_constants.probe_counts.x * m_constants.probe_counts.y * m_constants.probe_counts.z;

//    const auto rotScale = 0.0001f;
//    auto& rotation = m_constants.random_rotation;
//    rotation[0] = glm::vec4(randomVec3() * rotScale, 0);
//    rotation[1] = glm::vec4(randomVec3() * rotScale, 0);
//    rotation[2] = glm::vec4(randomVec3() * rotScale, 0);
}

void rtx::ddgi::initComputePipelines() {
    m_compute = ComputePipelines{ m_device, pipelines() };
    m_compute.createPipelines();
}

void rtx::ddgi::endFrame() {
}

bool rtx::ddgi::halfResolution() const {
    return m_constants.output_resolution_half == 1;
}

glm::vec2 rtx::ddgi::getResolution() const {
    auto res = glm::uvec2(m_cameraInfo->cpu().viewportSize);
    return !halfResolution() ? res : res/2u;
}

uint rtx::ddgi::indirectLight() const {
    return m_constants.indirect_texture_index;
}
