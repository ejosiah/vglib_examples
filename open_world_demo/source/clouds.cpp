#include "clouds.hpp"

#include <utility>
#include <stb_image_write.h>
#include "dds.hpp"

Clouds::Clouds(const VulkanDevice &device, const VulkanDescriptorPool &descriptorPool, const FileManager &fileManager,
               uint32_t width, uint32_t height, std::shared_ptr<SceneGBuffer> gBuffer, std::shared_ptr<AtmosphereLookupTable> atmosphereLUT)
    :m_device{&device}
    ,m_descriptorPool{&descriptorPool}
    , m_filemanager(&fileManager)
    , m_width{width}
    , m_height{ height }
    , m_gBuffer{std::move(gBuffer)}
    , m_atmosphereLUT{std::move( atmosphereLUT )}
{
    initUBO();
    createNoiseTexture();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createPipelines();

}

void Clouds::update(const SceneData &sceneData) {
    ubo->projection = sceneData.camera.proj;
    ubo->view = sceneData.camera.view;
    ubo->inverseProjection = glm::inverse(sceneData.camera.proj);
    ubo->inverseView = glm::inverse(sceneData.camera.view);
    ubo->camera = sceneData.eyes;
    ubo->sun_direction = glm::normalize(sceneData.sun.position);
    ubo->time = sceneData.time;
}

void Clouds::render(VkCommandBuffer commandBuffer) {

}

void Clouds::renderClouds() {
    device().graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){

        auto groupCountX = static_cast<uint32_t>(glm::round(m_width/32.f));
        auto groupCountY = static_cast<uint32_t>(glm::round(m_height/32.f));
        static std::array<VkDescriptorSet, 3> sets;
        sets[0] = descriptorSet;
        sets[1] = m_gBuffer->descriptorSet;
        sets[2] = m_atmosphereLUT->descriptorSet;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, cloud.pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, cloud.layout, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1u);
    });
}

void Clouds::renderUI(VkCommandBuffer commandBuffer) {

}

void Clouds::initUBO() {
    uboBuffer = device().createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(Ubo), "terrain");
    ubo = reinterpret_cast<Ubo*>(uboBuffer.map());

    ubo->earth_center = EARTH_CENTER;
    ubo->innerRadius = (3.5 * km + EARTH_RADIUS);
    ubo->outerRadius = (7.0 * km + EARTH_RADIUS);
    ubo->earthRadius = EARTH_RADIUS;
    ubo->eccentricity = 0;
    ubo->viewPortWidth = m_width;
    ubo->viewPortHeight = m_height;
}

void Clouds::createNoiseTexture() {
    textures::create(device(), textures.lowFrequencyNoise, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT, {128, 128, 128}, VK_SAMPLER_ADDRESS_MODE_REPEAT, sizeof(float));
    textures::create(device(), textures.highFrequencyNoise, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT, {32, 32, 32}, VK_SAMPLER_ADDRESS_MODE_REPEAT, sizeof(float));
    textures::fromFile(device(), textures.curlNoise, resource("cloud/curlNoise.png"));
    generateNoise();
}

void Clouds::generateNoise() {

    VulkanDescriptorSetLayout genDescriptorSetLayout =
        device().descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();

    VulkanPipelineLayout layout = device().createPipelineLayout({ genDescriptorSetLayout }, {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int)}});

    auto module = device().createShaderModule(resource("noise_gen.comp.spv"));
    auto stages = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    auto createInfo = initializers::computePipelineCreateInfo();
    createInfo.stage = stages;
    createInfo.layout = layout;
    auto pipeline = device().createComputePipeline(createInfo);
    
    auto sets = descriptorPool().allocate( { genDescriptorSetLayout, genDescriptorSetLayout });
    auto lowFreqDescriptorSet = sets[0];
    auto highFreqDescriptorSet = sets[1];

    auto writes = initializers::writeDescriptorSets<2>();
    writes[0].dstSet = lowFreqDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo imageInfo0{ VK_NULL_HANDLE, textures.lowFrequencyNoise.imageView, VK_IMAGE_LAYOUT_GENERAL };
    writes[0].pImageInfo = &imageInfo0;

    writes[1].dstSet = highFreqDescriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo imageInfo1{ VK_NULL_HANDLE, textures.highFrequencyNoise.imageView, VK_IMAGE_LAYOUT_GENERAL };
    writes[1].pImageInfo = &imageInfo1;

    device().updateDescriptorSets(writes);

    VulkanBuffer lowFreqNoiseBuffer = device().createStagingBuffer(textures.lowFrequencyNoise.image.size);
    VulkanBuffer highFreqNoiseBuffer = device().createStagingBuffer(textures.highFrequencyNoise.image.size);

    device().graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        textures.lowFrequencyNoise.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL, DEFAULT_SUB_RANGE,
            VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        textures.highFrequencyNoise.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL, DEFAULT_SUB_RANGE,
            VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        int noiseType = 0;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &lowFreqDescriptorSet, 0, VK_NULL_HANDLE);
        vkCmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &noiseType);
        vkCmdDispatch(commandBuffer, 16, 16, 16);

        noiseType = 1;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &highFreqDescriptorSet, 0, VK_NULL_HANDLE);
        vkCmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &noiseType);
        vkCmdDispatch(commandBuffer,4, 4, 4);

        textures.lowFrequencyNoise.image.copyToBuffer(commandBuffer, lowFreqNoiseBuffer, VK_IMAGE_LAYOUT_GENERAL);
        textures.highFrequencyNoise.image.copyToBuffer(commandBuffer, highFreqNoiseBuffer, VK_IMAGE_LAYOUT_GENERAL);

        textures.lowFrequencyNoise.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, DEFAULT_SUB_RANGE,
                                                          VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        textures.highFrequencyNoise.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, DEFAULT_SUB_RANGE,
                                                           VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    });



    VkDeviceSize layerSize = lowFreqNoiseBuffer.sizeAs<float>()/128;
    spdlog::info("low frequency noise layer size: {}", lowFreqNoiseBuffer.size + sizeof(glm::ivec3));

    auto data = reinterpret_cast<char*>(lowFreqNoiseBuffer.map());

    std::ofstream fout{ "c:/temp/low_frequency_noise.dat", std::ios::binary };
    glm::ivec3 dim{128, 128, 128};
    fout.write(reinterpret_cast<char*>(&dim), sizeof(dim));
    fout.write(data, lowFreqNoiseBuffer.size);
    fout.flush();
    fout.close();

    lowFreqNoiseBuffer.unmap();

    layerSize = highFreqNoiseBuffer.sizeAs<float>()/32;
    spdlog::info("high frequency noise layer size: {}", highFreqNoiseBuffer.size + sizeof(glm::ivec3));
    data = reinterpret_cast<char*>(highFreqNoiseBuffer.map());
    dim = {32, 32, 32};

    fout = std::ofstream { "c:/temp/high_frequency_noise.dat", std::ios::binary };
    fout.write(reinterpret_cast<char*>(&dim), sizeof(dim));
    fout.write(data, highFreqNoiseBuffer.size);
    fout.flush();
    fout.close();

    highFreqNoiseBuffer.unmap();

}


void Clouds::createDescriptorSetLayouts() {
    descriptorSetLayout =
        device().descriptorSetLayoutBuilder()
                .name("clouds")
                .binding(0)
                    .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                .binding(1)
                    .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                .binding(2)
                    .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                .binding(3)
                    .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();
}

void Clouds::updateDescriptorSets() {
    auto sets = m_descriptorPool->allocate({ descriptorSetLayout });
    descriptorSet = sets[0];

    auto writes = initializers::writeDescriptorSets<4>();

    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uboInfo{ uboBuffer, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &uboInfo;
    
    writes[1].dstSet = descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo lowFreqNoiseInfo{textures.lowFrequencyNoise.sampler, textures.lowFrequencyNoise.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[1].pImageInfo = &lowFreqNoiseInfo;

    writes[2].dstSet = descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo highFreqNoiseInfo{textures.highFrequencyNoise.sampler, textures.highFrequencyNoise.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[2].pImageInfo = &highFreqNoiseInfo;

    writes[3].dstSet = descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo curlFreqNoiseInfo{textures.curlNoise.sampler, textures.curlNoise.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[3].pImageInfo = &curlFreqNoiseInfo;

    device().updateDescriptorSets(writes);
}

void Clouds::createPipelines() {
    auto module = VulkanShaderModule{ resource("clouds.comp.spv"), device()};
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    cloud.layout = device().createPipelineLayout({ descriptorSetLayout, m_gBuffer->descriptorSetLayout, m_atmosphereLUT->descriptorSetLayout });

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = cloud.layout;

    cloud.pipeline = device().createComputePipeline(computeCreateInfo);
}

std::string Clouds::resource(const std::string &name) {
    auto res = m_filemanager->getFullPath(name);
    assert(res.has_value());
    return res->string();
}