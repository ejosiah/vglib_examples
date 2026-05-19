#include "vista/DisplacementShadowMap.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>

DisplacementShadowMap::DisplacementShadowMap(Context &context, const DisplacementMapInfo &displacement, const TerrainInfo& terrain)
: m_context{&context}
, m_displacementMap{displacement}
, m_terrain{ terrain}
, m_shadowMapImageIndex{ context.bindlessDescriptor->reserveImageSlots(1) }
{}

void DisplacementShadowMap::init() {
    initQueries();
    createShadowMapTexture();
    createComputePipelines();
    initConstants();
}

void DisplacementShadowMap::exec(VkCommandBuffer commandBuffer) {
//    if(!camera().moved()) return;
    auto& pc = m_Constants;
    pc.lightDir = glm::normalize(context().lightDirection);
    pc.softness = m_options.softness;
    pc.slopeBias = m_options.slopeBias;
    pc.enabled = to<int>(m_options.enabled);

    static int prevState = pc.enabled;
    if(prevState != pc.enabled) {
        profiler().clear(queryIds[QUERY_SHADOWS_GEN_ID]);
        prevState = pc.enabled;
    }

    pc.frustum = context().viewProjectionFrustum;

    const auto gx = (m_shadowMap.width + 15)/16;
    const auto gy = (m_shadowMap.height + 15)/16;
    auto descriptorSet = bindlessDescriptorSet();

    profiler().profile(queryIds[QUERY_SHADOWS_GEN_ID], commandBuffer, [&]{
        Barriers::pushAndFlush(commandBuffer, m_shadowMap.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("terrain_shadow_map"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("terrain_shadow_map"), 0, 1, &descriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, m_compute.layout("terrain_shadow_map"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
        vkCmdDispatch(commandBuffer, gx, gy, 1);

        Barriers::pushAndFlush(commandBuffer, m_shadowMap.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                               VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
}

void DisplacementShadowMap::setDisplacementScale(float scale) {
    m_Constants.displacementScale = std::max(scale, 0.0f);
}

void DisplacementShadowMap::createShadowMapTexture() {
    textures::create(device(), m_shadowMap, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                     { m_displacementMap.width/m_scale, m_displacementMap.height/m_scale, 1 },
                     VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    bindlessDescriptor().update({ &m_shadowMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context().dmap_shadow_tex_index });
    bindlessDescriptor().update({ &m_shadowMap, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_shadowMapImageIndex, VK_IMAGE_LAYOUT_GENERAL });
}

void DisplacementShadowMap::createComputePipelines() {
    m_compute = ComputePipelines{ m_context->device, metadata() };
    m_compute.createPipelines();
}

void DisplacementShadowMap::initConstants() {
    auto& pc = m_Constants;
    pc.lightDir = glm::normalize(context().lightDirection);
    pc.xzScale = { float(m_terrain.terrainSize.x)/m_shadowMap.width, float(m_terrain.terrainSize.y)/m_shadowMap.height };
    pc.heightRange = m_terrain.heightScale;

    const int targetMaxSteps = 512;
    const auto shadowMapDiagonal = std::hypot(float(m_shadowMap.width), float(m_shadowMap.height));
    pc.stepStride = std::max(1.0f, std::ceil(shadowMapDiagonal / float(targetMaxSteps)));
    pc.maxSteps   = int(std::ceil(shadowMapDiagonal / pc.stepStride));
    pc.slopeBias  = 0.001f;
    pc.softness   = 0.002f;
    pc.displacementScale = 1.0f;
    pc.shadow_image_index = m_shadowMapImageIndex;
    pc.dmap_tex_index = context().dmap_tex_index;

    m_options.softness = pc.softness;
    m_options.maxSteps = pc.maxSteps;
    m_options.slopeBias = pc.slopeBias;
}

std::vector<PipelineMetaData> DisplacementShadowMap::metadata() {
    return {
        {
            .name = "terrain_shadow_map",
            .shadePath = FileManager::resource("vista_terrain_shadow_map.comp.spv"),
            .layouts = { &bindlessDescriptorSetLayout()  },
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_Constants)} }
        }
    };
}

Context& DisplacementShadowMap::context()  {
    return *m_context;
}

void DisplacementShadowMap::controls() {
    if(ImGui::CollapsingHeader("Shadow", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("slope bias", &m_options.slopeBias, 0.001f, 0.1f, "%.4f");
        ImGui::SliderFloat("softness", &m_options.softness, 0.0f, 0.01f, "%.5f");
        ImGui::Checkbox("enabled", &m_options.enabled);
    }
}

float DisplacementShadowMap::printPerfStats() {
    const auto toMillis = 1e-6f;
    auto total = 0.0f;

    if (ImGui::TreeNode("Shadow map")) {
        // Leaf flags so they render as rows without opening/closing arrows
        ImGuiTreeNodeFlags leaf = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        auto name = queryIds[QUERY_SHADOWS_GEN_ID];
        auto duration = profiler().queries[name].movingAverage.value * toMillis;
        ImGui::TreeNodeEx(name.c_str(), leaf, "%s: %f ms", name.c_str(), duration);
        total += duration;
        ImGui::TreePop();
    }
    return total;
}

void DisplacementShadowMap::initQueries() {
    for(auto& id : queryIds) {
        profiler().addQuery(id);
    }
}
