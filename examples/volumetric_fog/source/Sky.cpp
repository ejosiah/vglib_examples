#include "Sky.hpp"
#include "primitives.h"
#include "GraphicsPipelineBuilder.hpp"


Sky::Sky(FileManager* fileManager,
         VulkanDevice *device,
         VulkanDescriptorPool *descriptorPool,
         VulkanRenderPass* renderPass,
         glm::uvec2 screenDimension,
         Camera* camera,
         Scene* scene,
         BindlessDescriptor* bindlessDescriptor)
: m_descriptor{ device, descriptorPool, bindlessDescriptor }
, m_filemanager{ fileManager }
, m_device{ device }
, m_pool{ descriptorPool }
, m_renderPass{ renderPass }
, m_screenDimension{ screenDimension, 1 }
, m_camera{ camera }
, m_scene{ scene }
{

}

void Sky::init() {
    m_descriptor.init();
    m_descriptor.load(resource("default.atmosphere"));
    createTexture();
    createSkyBox();
    createDescriptorSet();
    updateDescriptorSet();
    createPipeline();
}

void Sky::createTexture() {
    auto format = VK_FORMAT_R32G32B32A32_SFLOAT;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = { SKY_BOX_SIZE, SKY_BOX_SIZE, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 6;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    texture.image = m_device->createImage(imageInfo);

    VkSamplerCreateInfo samplerInfo = initializers::samplerCreateInfo();
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = samplerInfo.addressModeU;
    samplerInfo.addressModeW = samplerInfo.addressModeU;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.maxAnisotropy = 1.0f;

    texture.sampler = m_device->createSampler(samplerInfo);

    VkImageSubresourceRange resourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};
    texture.imageView = texture.image.createView(format, VK_IMAGE_VIEW_TYPE_CUBE, resourceRange);

    for(auto i = 0; i < 6; i++) {
        resourceRange.baseArrayLayer = i;
        resourceRange.layerCount = 1;
        views[i] = texture.image.createView(format, VK_IMAGE_VIEW_TYPE_2D, resourceRange);
    }

    resourceRange.baseArrayLayer = 0;
    resourceRange.layerCount = 6;
    texture.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, resourceRange);
    texture.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, resourceRange);
}

void Sky::createSkyBox() {
    auto geom = primitives::cube({1, 0, 0, 1});
    std::vector<glm::vec3> vertices;
    for(const auto& vertex : geom.vertices){
        vertices.push_back(vertex.position.xyz());
    }
    cube.vertices = m_device->createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    cube.indices = m_device->createDeviceLocalBuffer(geom.indices.data(), BYTE_SIZE(geom.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void Sky::createDescriptorSet() {
    skyBoxSetLayout =
        m_device->descriptorSetLayoutBuilder()
            .name("skybox_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();
    
    skyBoxSet = m_pool->allocate({skyBoxSetLayout }).front();
}

void Sky::updateDescriptorSet() {
    auto writes = initializers::writeDescriptorSets<1>();
    
    writes[0].dstSet = skyBoxSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo skyboxInfo{texture.sampler.handle, texture.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[0].pImageInfo = &skyboxInfo;
    
    m_device->updateDescriptorSets(writes);
    
}

void Sky::createPipeline() {
    //    @formatter:off

    auto builder = m_device->graphicsPipelineBuilder();
    generatePipeline.pipeline =
        builder
            .shaderStage()
                .vertexShader(resource("gen.skybox.vert.spv"))
                .fragmentShader(resource("gen.skybox.frag.spv"))
            .vertexInputState()
                .addVertexBindingDescription(0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
            .inputAssemblyState()
                .triangles()
            .viewportState()
                .viewport()
                    .origin(0, 0)
                    .dimension(SKY_BOX_SIZE, SKY_BOX_SIZE)
                    .minDepth(0)
                    .maxDepth(1)
                .scissor()
                    .offset(0, 0)
                    .extent(SKY_BOX_SIZE, SKY_BOX_SIZE)
                .add()
                .rasterizationState()
                    .cullNone()
                    .frontFaceCounterClockwise()
                    .polygonModeFill()
                .depthStencilState()
                .colorBlendState()
                    .attachment()
                    .add()
                .layout()
                    .addDescriptorSetLayout(m_descriptor.uboDescriptorSetLayout)
                    .addDescriptorSetLayout(m_descriptor.bindnessSetLayout())
                    .addDescriptorSetLayout(m_scene->sceneSetLayout)
                    .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4))
                .dynamicRenderPass()
                    .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .subpass(0)
                .name("generate_skybox")
            .build(generatePipeline.layout);
    //    @formatter:on
	
	createRenderPipeline();
}

void Sky::createRenderPipeline() {
    renderPipeline.pipeline = 
        m_device->graphicsPipelineBuilder()
            .shaderStage()
                .vertexShader(resource("skybox/skybox.vert.spv"))
                .fragmentShader(resource("skybox/skybox.frag.spv"))
            .vertexInputState()
                .addVertexBindingDescription(0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
            .inputAssemblyState()
                .triangles()
            .viewportState()
                .viewport()
                    .origin(0, 0)
                    .dimension(m_screenDimension.x, m_screenDimension.y)
                    .minDepth(0)
                    .maxDepth(1)
                .scissor()
                    .offset(0, 0)
                    .extent(m_screenDimension.x, m_screenDimension.y)
                .add()
            .rasterizationState()
                .cullFrontFace()
                .frontFaceCounterClockwise()
                .polygonModeFill()
            .multisampleState()
                .rasterizationSamples(VK_SAMPLE_COUNT_1_BIT)
            .depthStencilState()
                .enableDepthTest()
                .enableDepthWrite()
                .compareOpLessOrEqual()
                .minDepthBounds(0)
                .maxDepthBounds(1)
            .colorBlendState()
                .attachment()
                .add()
            .layout()
                .addDescriptorSetLayout(skyBoxSetLayout)
                .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Camera))
            .renderPass(*m_renderPass)
            .subpass(0)
            .name("skybox")
        .build(renderPipeline.layout);    
}

void Sky::render(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderPipeline.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderPipeline.layout.handle, 0, 1, &skyBoxSet, 0, VK_NULL_HANDLE);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, cube.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, cube.indices, 0, VK_INDEX_TYPE_UINT32);

    vkCmdPushConstants(commandBuffer
            , renderPipeline.layout.handle
            , VK_SHADER_STAGE_VERTEX_BIT
            , 0, sizeof(Camera), m_camera);
    vkCmdDrawIndexed(commandBuffer, cube.indices.sizeAs<uint32_t>(), 1, 0, 0, 0);
}

void Sky::update(float time) {
    generateSkybox();
}

void Sky::generateSkybox() {
    m_device->graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        static std::array<VkDescriptorSet, 3> sets;
        sets[0] = m_descriptor.uboDescriptorSet;
        sets[1] = m_descriptor.bindessDescriptorSet();
        sets[2] = m_scene->sceneSet;

        std::array<VkClearValue, 6> clearColors{};
        clearColors[0].color = {1.f, 0, 0, 1.f};
        clearColors[1].color = {1.f, 0, 0, 1.f};
        clearColors[2].color = {0, 1.f, 0, 1.f};
        clearColors[3].color = {0, 1.f, 0, 1.f};
        clearColors[4].color = {0, 0, 1.f, 1.f};
        clearColors[5].color = {0, 0, 1.f, 1.f};

        auto origin = m_scene->cpu->camera;
        auto projection = glm::perspective(glm::half_pi<float>(), 1.0f, 0.1f, 147.17e6f);
        for(auto i = 0; i < 6; i++) {
            auto face = faces[i];
            VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
            info.flags = 0;
            info.renderArea = {{0, 0}, {SKY_BOX_SIZE, SKY_BOX_SIZE}};
            info.layerCount = 1;
            info.colorAttachmentCount = 1;

            VkRenderingAttachmentInfo attachmentInfo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
            attachmentInfo.imageView = views[i].handle;
            attachmentInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            attachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
            attachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachmentInfo.clearValue = clearColors[i];
            info.pColorAttachments = &attachmentInfo;

            vkCmdBeginRendering(commandBuffer, &info);

            VkDeviceSize offset = 0;
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, generatePipeline.pipeline.handle);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, generatePipeline.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, cube.vertices, &offset);
            vkCmdBindIndexBuffer(commandBuffer, cube.indices, 0, VK_INDEX_TYPE_UINT32);

            auto model = glm::translate(glm::mat4{1}, origin);
            auto view = glm::lookAt(origin, origin + face.direction, face.up);
            auto transform = projection * view * model;
            vkCmdPushConstants(commandBuffer
                    , generatePipeline.layout.handle
                    , VK_SHADER_STAGE_VERTEX_BIT
                    , 0, sizeof(glm::mat4), glm::value_ptr(transform));
            vkCmdDrawIndexed(commandBuffer, cube.indices.sizeAs<uint32_t>(), 1, 0, 0, 0);

            vkCmdEndRendering(commandBuffer);
        }
    });
}

void Sky::reload(VulkanRenderPass* newRenderPass, int width, int height) {
    m_screenDimension.x = width;
    m_screenDimension.y = height;
    m_renderPass = newRenderPass;
    createRenderPipeline();
}

std::string Sky::resource(const std::string &name) {
    auto res = m_filemanager->getFullPath(name);
    assert(res.has_value());
    return res->string();
}
