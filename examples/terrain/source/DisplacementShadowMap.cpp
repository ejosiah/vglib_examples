#include "DisplacementShadowMap.hpp"
#include <imgui.h>

DisplacementShadowMap::DisplacementShadowMap(Context &context, const DisplacementMapInfo &displacement, const TerrainInfo& terrain)
: m_context{&context}
, m_displacementMap{displacement}
, m_terrain{ terrain}
, m_shadowMapImageIndex{ context.bindlessDescriptor->reserveImageSlots(1) }
{}

void DisplacementShadowMap::init() {
    createShadowMapTexture();
    createComputePipelines();
    initConstants();
}

void DisplacementShadowMap::exec(VkCommandBuffer commandBuffer) {
//    if(!camera().moved()) return;
    auto& pc = m_Constants;
    pc.lightDir = glm::normalize(*context().lightDirection);
    pc.softness = m_options.softness;
    pc.slopeBias = m_options.slopeBias;
    pc.enabled = to<int>(m_options.enabled);

    const auto& cam = camera().cam();
    auto vp = cam.proj * cam.view;
    Frustum::extractFrustum(pc.frustum, vp);

    const auto gx = (m_displacementMap.width + 15)/16;
    const auto gy = (m_displacementMap.height + 15)/16;
    auto descriptorSet = bindlessDescriptorSet();

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
}

void DisplacementShadowMap::createShadowMapTexture() {
    textures::create(device(), m_shadowMap, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, { m_displacementMap.width, m_displacementMap.height, 1 });
    bindlessDescriptor().update({ &m_shadowMap, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context().dmap_shadow_tex_index });
    bindlessDescriptor().update({ &m_shadowMap, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_shadowMapImageIndex, VK_IMAGE_LAYOUT_GENERAL });
}

void DisplacementShadowMap::createComputePipelines() {
    m_compute = ComputePipelines{ m_context->device, metadata() };
    m_compute.createPipelines();
}

void DisplacementShadowMap::initConstants() {
    auto& pc = m_Constants;
    pc.lightDir = glm::normalize(*context().lightDirection);
    pc.xzScale = { m_terrain.width/m_displacementMap.width, m_terrain.height/m_displacementMap.height };
    pc.heightRange = {m_terrain.zMin, m_terrain.zMax };

    const int targetMaxSteps = 512;
    using namespace std;
    pc.stepStride = max(1.0f, ceil(max(m_displacementMap.width, m_displacementMap.height)/float(targetMaxSteps)));
    pc.maxSteps   = int(ceil(float(max(m_displacementMap.width, m_displacementMap.height)) / pc.stepStride));
    pc.slopeBias  = 0.001f;
    pc.softness   = 0.002f;
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
            .shadePath = FileManager::resource("terrain_shadow_map.comp.spv"),
            .layouts = { &bindlessDescriptorSetLayout()  },
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_Constants)} }
        }
    };
}

Context& DisplacementShadowMap::context()  {
    return *m_context;
}

void DisplacementShadowMap::controls() {
    ImGui::Begin("Shadow");
    ImGui::SetWindowSize({});
    ImGui::SliderFloat("slope bias", &m_options.slopeBias, 0.001, 0.1);
    ImGui::SliderFloat("softness", &m_options.softness, 0, 0.0006);
    ImGui::Checkbox("enabled", &m_options.enabled);
    ImGui::End();
}
