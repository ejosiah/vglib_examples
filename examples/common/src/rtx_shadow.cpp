#include <utility>

#include "rtx/shadow.hpp"
#include "Texture.h"
#include "filemanager.hpp"
#include "Barrier.hpp"

rtx::shadow::shadow(const Params& p)
: m_device{ &p.device }
, m_bindlessDescriptor{ &p.bindlessDescriptor }
, m_descriptorPool{ &p.descriptorPool }
, m_cameraInfo{ p.cameraInfo }
, m_lightDescriptorSetLayout{ p.lightDescriptorSetLayout }
, m_bvhDescriptorSetLayoutLayout{ p.bvhDescriptorSetLayoutLayout }
, m_lightsDescriptorSet{ p.lightDescriptorSet }
, m_bvhDescriptorSet{ p.bvhDescriptorSet }
, m_numLights{ p.numLights }
, m_constants{  .depthBufferIndex = p.depthBufferIndex, .normalBufferIndex = p.normalBufferIndex }
{
    assert(p.numLights > 0);
}

void rtx::shadow::init() {
    initTextures();
    createConstantsBuffer();
    createDescriptorSetLayouts();
    updateDescriptorSet();
    initComputePipelines();
}

void rtx::shadow::exec(VkCommandBuffer commandBuffer) {
    computeMotionVectors(commandBuffer);
    Barrier::computeWriteToRead(commandBuffer);

    computeVisibilityVariance(commandBuffer);
    Barrier::computeWriteToRead(commandBuffer);

    computeVisibility(commandBuffer);
    Barrier::computeWriteToFragmentRead(commandBuffer);
}

void rtx::shadow::computeMotionVectors(VkCommandBuffer commandBuffer) {
    const auto gx = static_cast<uint32_t>(m_cameraInfo->cpu().viewportSize.x + 31) / 32u;
    const auto gy = static_cast<uint32_t>(m_cameraInfo->cpu().viewportSize.y + 31) / 32u;

    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = m_constantsDescriptorSet;
    sets[1] = *m_cameraInfo->descriptorSet();
    sets[2] = m_bindlessDescriptor->descriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("motion_vectors"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("motion_vectors"), 0, COUNT(sets), sets.data(), 0, 0);
    vkCmdDispatch(commandBuffer, gx, gy, 1u);

}

void rtx::shadow::computeVisibilityVariance(VkCommandBuffer commandBuffer) {
    const auto gx = static_cast<uint32_t>(m_cameraInfo->cpu().viewportSize.x/2 + 31) / 32u;
    const auto gy = static_cast<uint32_t>(m_cameraInfo->cpu().viewportSize.y/2 + 31) / 32u;
    const auto gz = m_numLights;

    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = m_constantsDescriptorSet;
    sets[1] = m_bindlessDescriptor->descriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("visibility_variance"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("visibility_variance"), 0, COUNT(sets), sets.data(), 0, 0);
    vkCmdDispatch(commandBuffer, gx, gy, gz);

}


void rtx::shadow::computeVisibility(VkCommandBuffer commandBuffer) {
    const auto gx = static_cast<uint32_t>(m_cameraInfo->cpu().viewportSize.x/2 + 7) / 8u;
    const auto gy = static_cast<uint32_t>(m_cameraInfo->cpu().viewportSize.y/2 + 7) / 8u;
    const auto gz = m_numLights;

    static std::array<VkDescriptorSet, 5> sets;
    sets[0] = m_constantsDescriptorSet;
    sets[1] = *m_cameraInfo->descriptorSet();
    sets[2] = m_lightsDescriptorSet;
    sets[3] = m_bvhDescriptorSet;
    sets[4] = m_bindlessDescriptor->descriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("visibility"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("visibility"), 0, COUNT(sets), sets.data(), 0, 0);
    vkCmdDispatch(commandBuffer, gx, gy, gz);

}


void rtx::shadow::initComputePipelines() {
    m_compute = ComputePipelines{ m_device, pipelines() };
    m_compute.createPipelines();
}

void rtx::shadow::initTextures() {
    const auto w = m_cameraInfo->cpu().viewportSize.x;
    const auto h = m_cameraInfo->cpu().viewportSize.y;
    const auto w2 = w/2;
    const auto h2 = h/2;

    // todo ceil32
    textures::create(*m_device, m_viewNormal, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {w, h, 1});
    textures::create(*m_device, m_motionVector, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16_SFLOAT, {w, h, 1});
    textures::create(*m_device, m_visibilityCache, VK_IMAGE_TYPE_3D, VK_FORMAT_R16G16B16A16_SFLOAT, {w2, h2, m_numLights});
    textures::create(*m_device, m_variation, VK_IMAGE_TYPE_2D, VK_FORMAT_R16_SFLOAT, {w2, h2, m_numLights});
    textures::create(*m_device, m_variationCache, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {w2, h2, m_numLights});
    textures::create(*m_device, m_filteredVariation, VK_IMAGE_TYPE_2D, VK_FORMAT_R16_SFLOAT, {w2, h2, m_numLights});
    textures::create(*m_device, m_filteredVisibility, VK_IMAGE_TYPE_2D, VK_FORMAT_R16_SFLOAT, {w2, h2, m_numLights});
    textures::create(*m_device, m_sampleCountCache, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UINT, {w2, h2, m_numLights});

    m_viewNormal.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    m_motionVector.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    m_constants.motionVectorImageIndex = m_bindlessDescriptor->update(m_motionVector, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
    m_constants.viewNormalImageIndex = m_bindlessDescriptor->update(m_viewNormal, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);

    m_constants.visibilityCacheImageIndex = m_bindlessDescriptor->update(m_visibilityCache, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
    m_constants.variationImageIndex = m_bindlessDescriptor->update(m_variation, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
    m_constants.variationCacheImageIndex = m_bindlessDescriptor->update(m_variationCache, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
    m_constants.filteredVariationImageIndex = m_bindlessDescriptor->update(m_filteredVariation, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
    m_constants.filteredVisibilityImageIndex = m_bindlessDescriptor->update(m_filteredVisibility, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
    m_constants.sampleCountCacheImageIndex = m_bindlessDescriptor->update(m_sampleCountCache, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);

    m_constants.motionVectorTextureIndex = m_bindlessDescriptor->update(m_motionVector, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_LAYOUT_GENERAL);
    m_constants.normalsTextureIndex = m_bindlessDescriptor->update(m_viewNormal, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_LAYOUT_GENERAL);

    m_constants.visibilityCacheTextureIndex = m_bindlessDescriptor->update(m_visibilityCache, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_LAYOUT_GENERAL);
    m_constants.variationTextureIndex = m_bindlessDescriptor->update(m_variation, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_LAYOUT_GENERAL);
    m_constants.variationCacheTextureIndex = m_bindlessDescriptor->update(m_variationCache, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_LAYOUT_GENERAL);
    m_constants.filteredVariationTextureIndex = m_bindlessDescriptor->update(m_filteredVariation, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_LAYOUT_GENERAL);
    m_constants.filteredVisibilityTextureIndex = m_bindlessDescriptor->update(m_filteredVisibility, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_LAYOUT_GENERAL);
    m_constants.sampleCountCacheTextureIndex = m_bindlessDescriptor->update(m_sampleCountCache, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_LAYOUT_GENERAL);
}

void rtx::shadow::createDescriptorSetLayouts() {
    m_constantsDescriptorSetLayout =
        m_device->descriptorSetLayoutBuilder()
            .name("shadow_constants_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();
}

void rtx::shadow::updateDescriptorSet() {
    auto sets = m_descriptorPool->allocate({ m_constantsDescriptorSetLayout });
    m_constantsDescriptorSet = sets[0];

    auto writes = initializers::writeDescriptorSets<1>();

    writes[0].dstSet = m_constantsDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uniformInfo{m_constantsBuffer, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &uniformInfo;

    m_device->updateDescriptorSets(writes);

}

std::vector<PipelineMetaData> rtx::shadow::pipelines() {
    return {
            {
                .name = "motion_vectors",
                .shadePath = FileManager::resource("rtx_shadow_motion_vector.comp.spv"),
                .layouts = {
                    &m_constantsDescriptorSetLayout,
                    m_cameraInfo->descriptorSetLayout(),
                    m_bindlessDescriptor->ncDescriptorSetLayout()
                },
            },
            {
                .name = "visibility_variance",
                .shadePath = FileManager::resource("rtx_shadow_visibility_variance.comp.spv"),
                .layouts = {
                    &m_constantsDescriptorSetLayout,
                    m_bindlessDescriptor->ncDescriptorSetLayout()
                },
            },
            {
                .name = "visibility",
                .shadePath = FileManager::resource("rtx_shadow_visibility.comp.spv"),
                .layouts = {
                    &m_constantsDescriptorSetLayout,
                    m_cameraInfo->descriptorSetLayout(),
                    &m_lightDescriptorSetLayout,
                    &m_bvhDescriptorSetLayoutLayout,
                    m_bindlessDescriptor->ncDescriptorSetLayout(),
                }
            },
    };
}

uint32_t rtx::shadow::motionVectors() const {
    return m_constants.motionVectorTextureIndex;
}


uint32_t rtx::shadow::normals() const {
    return m_constants.normalsTextureIndex;
}

uint32_t rtx::shadow::variance() const {
    return m_constants.variationTextureIndex;
}

void rtx::shadow::newFrame() {
    m_constants.frameIndex++;
}

void rtx::shadow::createConstantsBuffer() {
    m_constantsBuffer = m_device->createDeviceLocalBuffer(&m_constants, sizeof(m_constants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
}
