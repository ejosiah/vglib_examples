#include "PointShadowMap.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "filemanager.hpp"
#include "Vertex.h"
#include "halfedge.hpp"
#include "primitives.h"

#include <cassert>
#include <utility>

PointShadowMap::PointShadowMap(
    VulkanDevice &device
    , VulkanDescriptorPool &descriptorPool
    , uint32_t inflightFrames
    , VkFormat depthFormat
    , uint32_t size)
    : _device(&device)
    , _descriptorPool(&descriptorPool)
    , _depthFormat(depthFormat)
    , _size(size)
    , _shadowMap(inflightFrames) {
        assert(size != 0 && "shadow map size should not be zero");
        assert(inflightFrames != 0 && "inflightFrames should be at least 1");
    }


void PointShadowMap::init() {
    createShadowMapTexture();
    createRenderInfo();
    createUniforms();
    createCube();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createPipeline();
}

void PointShadowMap::update(const glm::vec3 &lightPosition) {
    _constants.lightPosition = lightPosition;

    for(auto i = 0; i < 6; ++i) {
        _uniforms.cpu[VIEW_MATRIX_START + i] = glm::lookAt(lightPosition, lightPosition + faces[i].direction, faces[i].up);
    }
}

void PointShadowMap::updateWorldMatrix(const glm::mat4 &matrix) {
    _constants.worldTransform = matrix;
}

void PointShadowMap::setRange(float range) const {
    _uniforms.cpu[PROJECTION_MATRIX] = glm::perspective(glm::half_pi<float>(), 1.0f, 0.1f, range);
}

void PointShadowMap::capture(const PointShadowMap::Scene& scene, VkCommandBuffer commandBuffer, int currentFrame) {
    _offscreen.render(commandBuffer, renderInfo(currentFrame), [&]{
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _layout.handle, 0, 1, &_descriptorSet, 0, VK_NULL_HANDLE);
        vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_FRONT_BIT);
        vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        vkCmdPushConstants(commandBuffer, _layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(_constants), &_constants);

        scene(_layout);
    });
}

const Texture &PointShadowMap::shadowMap(int index) const {
    return _shadowMap[index].color;
}

void PointShadowMap::createShadowMapTexture() {
    for(auto& shadowMap : _shadowMap) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R32_SFLOAT;
        imageInfo.extent = {_size, _size, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 6;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        shadowMap.color.image = device().createImage(imageInfo);
        VkImageSubresourceRange resourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};
        shadowMap.color.imageView = shadowMap.color.image.createView(VK_FORMAT_R32_SFLOAT, VK_IMAGE_VIEW_TYPE_CUBE, resourceRange);
        shadowMap.color.spec = imageInfo;
        shadowMap.color.layers = 6;

        shadowMap.color.image.transitionLayout(device().graphicsCommandPool(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, resourceRange);
        shadowMap.color.image.transitionLayout(device().graphicsCommandPool(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, resourceRange);

        imageInfo.format = _depthFormat;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        resourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

        shadowMap.depth.image = device().createImage(imageInfo);
        shadowMap.depth.imageView = shadowMap.depth.image.createView(_depthFormat, VK_IMAGE_VIEW_TYPE_CUBE, resourceRange);
        shadowMap.depth.spec = imageInfo;


        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        shadowMap.color.sampler = device().createSampler(samplerInfo);
    }
}

void PointShadowMap::createRenderInfo() {
    for(auto& shadowMap : _shadowMap) {
        _renderInfo.push_back({
            .colorAttachments = { { &shadowMap.color, VK_FORMAT_R32_SFLOAT, glm::vec4(MAX_FLOAT) } },
            .depthAttachment = { { &shadowMap.depth, _depthFormat } },
            .renderArea = { _size, _size },
            .numLayers = 6,
            .viewMask =  0b00111111
        });
    }   
}

void PointShadowMap::createDescriptorSetLayouts() {
    _descriptorSetLayout =
        device().descriptorSetLayoutBuilder()
            .name("shadow_map_light_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();

    _meshDescriptorSetLayout =
        device().descriptorSetLayoutBuilder()
            .name("shadow_map_mesh_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
        .createLayout();
}

void PointShadowMap::updateDescriptorSets() {
    _descriptorSet = _descriptorPool->allocate({ _descriptorSetLayout }).front();
    
    auto writes = initializers::writeDescriptorSets<2>();
    writes[0].dstSet = _descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo info{ _uniforms.gpu, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &info;
   
    writes[1].dstSet = _descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo imageInfo{
        _shadowMap[0].color.sampler.handle
        , _shadowMap[0].color.imageView.handle
        , VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[1].pImageInfo = &imageInfo;

    device().updateDescriptorSets(writes);
}

void PointShadowMap::createUniforms() {
    std::vector<glm::mat4> lightMatrix(7, glm::mat4{1});
    lightMatrix[PROJECTION_MATRIX] = glm::perspective(glm::half_pi<float>(), 1.0f, 0.1f, DEFAULT_RANGE);
    _uniforms.gpu = device().createCpuVisibleBuffer(lightMatrix.data(), BYTE_SIZE(lightMatrix), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    _uniforms.cpu = _uniforms.gpu.span<glm::mat4>();
    device().setName<VK_OBJECT_TYPE_BUFFER>("shadow_map_light_matrices", _uniforms.gpu.buffer);
}

void PointShadowMap::createPipeline() {
    _pipeline =
        device().graphicsPipelineBuilder()
            .allowDerivatives()
            .shaderStage()
                .vertexShader(FileManager::resource("gltf_point_shadow_map.vert.spv"))
                .fragmentShader(FileManager::resource("point_shadow_map.frag.spv"))
            .vertexInputState().clear()
                .addVertexBindingDescription(VertexMultiAttributes::bindingDescription())
                .addVertexAttributeDescriptions(VertexMultiAttributes::attributeDescription())
            .inputAssemblyState()
                .triangles()
            .viewportState()
                .viewport()
                    .origin(0, 0)
                    .dimension(_size, _size)
                    .minDepth(0)
                    .maxDepth(1)
                .scissor()
                    .offset(0, 0)
                    .extent(_size, _size)
                .add()
            .rasterizationState()
                .cullFrontFace()
                .frontFaceCounterClockwise()
                .polygonModeFill()
            .depthStencilState()
                .enableDepthWrite()
                .enableDepthTest()
                .compareOpLessOrEqual()
                .minDepthBounds(0)
                .maxDepthBounds(1)
            .colorBlendState()
                .attachment()
                .add()
            .dynamicState()
                .cullMode()
                .primitiveTopology()
            .layout()
                .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(_constants))
                .addDescriptorSetLayout(_descriptorSetLayout)
                .addDescriptorSetLayout(_meshDescriptorSetLayout)
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32_SFLOAT)
                .depthAttachment(_depthFormat)
                .viewMask(0b00111111)
            .subpass(0)
            .name("point_shadow_map")
        .build(_layout);

    if(_renderPass) {
    debug.pipeline =
        device().graphicsPipelineBuilder()
            .shaderStage()
                .vertexShader(FileManager::resource("point_shadow_map_debug.vert.spv"))
                .fragmentShader(FileManager::resource("point_shadow_map_debug.frag.spv"))
                .vertexInputState()
                    .addVertexBindingDescription(0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX)
                    .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
                .inputAssemblyState()
                    .triangles()
                .viewportState()
                    .viewport()
                        .origin(0, 0)
                        .dimension(_screenResolution.x, _screenResolution.y)
                        .minDepth(0)
                        .maxDepth(1)
                    .scissor()
                        .offset(0, 0)
                        .extent(_screenResolution.x, _screenResolution.y)
                    .add()
                .rasterizationState()
                    .cullFrontFace()
                    .frontFaceCounterClockwise()
                    .polygonModeFill()
                .depthStencilState()
                    .enableDepthWrite()
                    .enableDepthTest()
                    .compareOpLessOrEqual()
                    .minDepthBounds(0)
                    .maxDepthBounds(1)
                .colorBlendState()
                    .attachment()
                    .add()
                .layout()
                    .addPushConstantRange(Camera::pushConstant())
                    .addDescriptorSetLayout(_descriptorSetLayout)
                .renderPass(_renderPass->renderPass)
                .subpass(0)
            .name("debug_point_shadow_map")
        .build(debug.layout);
    }
}

Offscreen::RenderInfo &PointShadowMap::renderInfo(int index) {
    assert(!_renderInfo.empty());
    return _renderInfo[index];
}

VulkanDevice &PointShadowMap::device() {
    return *_device;
}

std::array<PointShadowMap::Face, 6>PointShadowMap::faces = {{
         {{1.0, 0.0, 0.0}, {0.0,-1.0, 0.0}},
         {{-1.0, 0.0, 0.0}, {0.0,-1.0, 0.0}},
         {{0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}},
         {{0.0, -1.0, 0.0}, {0.0, 0.0, -1.0}},
         {{0.0, 0.0, 1.0}, {0.0,-1.0, 0.0}},
         {{0.0, 0.0, -1.0}, {0.0,-1.0, 0.0}}
 }};

void PointShadowMap::addBarrier(VkCommandBuffer commandBuffer) {
    static VkMemoryBarrier2 barrier {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT
    };

    static VkDependencyInfo info{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier
    };

    vkCmdPipelineBarrier2(commandBuffer, &info);

}

void PointShadowMap::createCube() {
    auto prim = primitives::cube({1, 0, 0, 1});
    std::vector<glm::vec3> vertices;
    for(const auto& vertex : prim.vertices){
        vertices.push_back(vertex.position.xyz());
    }
    cube.vertices = device().createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices)
            , VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    cube.indexes = device().createDeviceLocalBuffer(prim.indices.data(), BYTE_SIZE(prim.indices)
            , VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void PointShadowMap::render(VkCommandBuffer commandBuffer, const Camera &camera) {
    static VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debug.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debug.layout.handle, 0, 1, &_descriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, debug.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Camera), &camera);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &cube.vertices.buffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, cube.indexes, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, cube.indexes.sizeAs<uint32_t>(), 1, 0, 0, 0);
}

void PointShadowMap::setRenderPass(VulkanRenderPass &renderPass, glm::uvec2 resolution) {
    _renderPass = &renderPass;
    _screenResolution = resolution;
}
