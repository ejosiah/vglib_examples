#include "NormalMapping.hpp"
#include "AppContext.hpp"

NormalMapping::NormalMapping(VulkanDevice &device)
: ComputePipelines(&device){}

void NormalMapping::init() {
    createDescriptorSettLayouts();
    createPipelines();
}

void NormalMapping::createDescriptorSettLayouts() {
    textureDescriptorSetLayout =
        device->descriptorSetLayoutBuilder()
            .name("template_texture_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    imageDescriptorSetLayout =
        device->descriptorSetLayoutBuilder()
            .name("template_image_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();
}

std::vector<PipelineMetaData> NormalMapping::pipelineMetaData() {
    sConstants.entries = { {0, 0, sizeof(uint32_t)}, {1, sizeof(uint32_t), sizeof(uint32_t)} };
    sConstants.data = glm::value_ptr(workGroupSize);
    sConstants.dataSize = sizeof(workGroupSize);

    return {
            {
                "normal_map",
                AppContext::resource("normal_mapping.comp.spv"),
                { &textureDescriptorSetLayout, &imageDescriptorSetLayout },
                { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants) } },
                sConstants
            }
    };
}

void NormalMapping::execute(VkCommandBuffer commandBuffer, VkDescriptorSet srcDescriptorSet, VkDescriptorSet dstDescriptorSet, glm::uvec2 textureSize) {
    auto gc = textureSize / workGroupSize;
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = srcDescriptorSet;
    sets[1] = dstDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("normal_map"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("normal_map"), 0, COUNT(sets), sets.data(), 0, 0);
    vkCmdPushConstants(commandBuffer, layout("normal_map"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, gc.x, gc.y, 1);
}

