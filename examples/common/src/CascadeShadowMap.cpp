#include "CascadeShadowMap.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "filemanager.hpp"
#include "Vertex.h"
#include "AppContext.hpp"
#include <spdlog/spdlog.h>

CascadeShadowMap::CascadeShadowMap(
        VulkanDevice &device
        , VulkanDescriptorPool &descriptorPool
        , uint32_t inflightFrames
        , VkFormat depthFormat
        , uint32_t numCascades
        , uint32_t size)
        : _device(&device)
        , _descriptorPool(&descriptorPool)
        , _depthFormat(depthFormat)
        , _numCascades(numCascades)
        , _size(size)
        , _cascadeSplits(numCascades)
        , _shadowMap(inflightFrames) {
    assert(size != 0 && "shadow map size should not be zero");
    assert(numCascades > 0 && "numCascades should be at least 2");
    assert(inflightFrames != 0 && "inflightFrames should be at least 1");
}

void CascadeShadowMap::init() {
    createShadowMapTexture();
    createRenderInfo();
    createUniforms();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createPipeline();
}

/*
    Calculate frustum split depths and matrices for the shadow map cascades
    Based on https://johanmedestrom.wordpress.com/2016/03/18/opengl-cascaded-shadow-maps/
*/
void CascadeShadowMap::update(const AbstractCamera& camera, const glm::vec3 &lightDirection, std::span<float> splitDepth, std::span<glm::vec3> extents) {
    if(!extents.empty()) {
        assert(extents.size() == _numCascades * 2);
    }
    const auto nearClip = camera.near();
    const auto farClip = camera.far();
    const auto clipRange = farClip - nearClip;

    const auto minZ = nearClip;
    const auto maxZ = nearClip + clipRange;

    const auto range = maxZ - minZ;
    const auto ratio = maxZ / minZ;

    glm::vec3 sMin{-100, -2, -100};
    glm::vec3 sMax{100, 20, 100};

    // Calculate split depths based on view camera frustum
    // Based on method presented in https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
    for (uint32_t i = 0; i < _numCascades; i++) {
        float p = (i + 1) / static_cast<float>(_numCascades);
        float log = minZ * std::pow(ratio, p);
        float uniform = minZ + range * p;
        float d = _splitLambda * (log - uniform) + uniform;
        _cascadeSplits[i] = (d - nearClip) / clipRange;
    }

    float lastSplitDist = 0.0;
    static auto once = true;
    for (uint32_t i = 0; i < _numCascades; i++) {
        float splitDist = _cascadeSplits[i];

        glm::vec3 frustumCorners[8] = {
            glm::vec3(-1.0f,  1.0f, 0.0f),
            glm::vec3( 1.0f,  1.0f, 0.0f),
            glm::vec3( 1.0f, -1.0f, 0.0f),
            glm::vec3(-1.0f, -1.0f, 0.0f),
            glm::vec3(-1.0f,  1.0f,  1.0f),
            glm::vec3( 1.0f,  1.0f,  1.0f),
            glm::vec3( 1.0f, -1.0f,  1.0f),
            glm::vec3(-1.0f, -1.0f,  1.0f),
        };


        // Project frustum corners into world space
        glm::mat4 invCam = glm::inverse(camera.cam().proj * camera.cam().view);
        for (auto & frustumCorner : frustumCorners) {
            glm::vec4 invCorner = invCam * glm::vec4(frustumCorner, 1.0f);
            frustumCorner = invCorner / invCorner.w;
        }

        for (uint32_t j = 0; j < 4; j++) {
            glm::vec3 dist = frustumCorners[j + 4] - frustumCorners[j];
            frustumCorners[j + 4] = frustumCorners[j] + (dist * splitDist);
            frustumCorners[j] = frustumCorners[j] + (dist * lastSplitDist);
        }

        // Get frustum center
        auto frustumCenter = glm::vec3(0.0f);
        for (auto frustumCorner : frustumCorners) {
            frustumCenter += frustumCorner;
        }
        frustumCenter /= 8.0f;

        float radius = 0.0f;
        for (auto frustumCorner : frustumCorners) {
            float distance = glm::length(frustumCorner - frustumCenter);
            radius = glm::max(radius, distance);
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        auto maxExtents = glm::vec3(radius);
        auto minExtents = -maxExtents;

        if(!extents.empty()){
            extents[i * 2] =  minExtents;
            extents[i * 2 + 1] =  maxExtents;
        }

        auto lightDir = normalize(lightDirection);
        auto lightViewMatrix = glm::lookAt(frustumCenter + lightDir * -minExtents.z, frustumCenter, glm::vec3(0.0f, 1.0f, 0.0f));
        auto lightOrthoMatrix = vkn::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.0f, maxExtents.z - minExtents.z);

        // Store split distance and matrix in cascade
        splitDepth[i] = (camera.near() + splitDist * clipRange) * -1.0f;
        _uniforms.cpu[i]= lightOrthoMatrix * lightViewMatrix;

        lastSplitDist = _cascadeSplits[i];
    }
}

void CascadeShadowMap::capture(const CascadeShadowMap::Scene &scene, VkCommandBuffer commandBuffer, int currentFrame) {
//    _offscreen.render(commandBuffer, _renderInfo[currentFrame], [&]{
//        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline.handle);
//        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _layout.handle, 0, 1, &_descriptorSet, 0, VK_NULL_HANDLE);
//        vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_FRONT_BIT);
//        vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
//        vkCmdPushConstants(commandBuffer, _layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(_constants), &_constants);
//
//        scene(_layout);
//    });

    auto& renderInfo = _renderInfo[currentFrame];
    for(auto i = 0; i < _numCascades; ++i) {
        renderInfo.depthAttachment->imageView = imageViews[currentFrame][i];
        _constants.cascadeIndex = i;

        _offscreen.render(commandBuffer, renderInfo, [&]{
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline.handle);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _layout.handle, 0, 1, &_descriptorSet, 0, VK_NULL_HANDLE);
            vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_FRONT_BIT);
            vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
            vkCmdPushConstants(commandBuffer, _layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(_constants), &_constants);

            scene(_layout);
        });
    }
}

const Texture &CascadeShadowMap::shadowMap(int index) const {
    return _shadowMap[index];
}

void CascadeShadowMap::setRenderPass(VulkanRenderPass &renderPass, glm::uvec2 resolution) {
    _renderPass = &renderPass;
    _screenResolution = resolution;
}

void CascadeShadowMap::createShadowMapTexture() {
    for(auto& shadowMap : _shadowMap) {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.flags = VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = _depthFormat;
        imageInfo.extent = {_size, _size, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = _numCascades;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        shadowMap.image = device().createImage(imageInfo);
        VkImageSubresourceRange resourceRange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, _numCascades};
        shadowMap.imageView = shadowMap.image.createView(_depthFormat, VK_IMAGE_VIEW_TYPE_2D_ARRAY, resourceRange);
        shadowMap.spec = imageInfo;
        shadowMap.layers = _numCascades;

        shadowMap.image.transitionLayout(device().graphicsCommandPool(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, resourceRange);
        shadowMap.image.transitionLayout(device().graphicsCommandPool(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, resourceRange);



        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;

        shadowMap.sampler = device().createSampler(samplerInfo);

    }
    createImageViews();
}

void CascadeShadowMap::createImageViews() {
    for(auto i = 0; i < 2; ++i) {
        for(auto j = 0u; j < _numCascades; ++j) {
            VkImageSubresourceRange resourceRange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, j, 1};
            auto imageView = _shadowMap[i].image.createView(_depthFormat, VK_IMAGE_VIEW_TYPE_2D, resourceRange);
            imageViews[i].push_back(imageView);
        }
    }
}

void CascadeShadowMap::createRenderInfo() {
    for(auto& shadowMap : _shadowMap) {
        _renderInfo.push_back({
              .depthAttachment = { { shadowMap.imageView, _depthFormat } },
              .renderArea = { _size, _size },
              .numLayers = 1,
              .viewMask =  viewMask()
          });
    }
}

void CascadeShadowMap::createUniforms() {
    std::vector<glm::mat4> lightMatrix(_numCascades, glm::mat4{1});
    _uniforms.gpu = device().createCpuVisibleBuffer(lightMatrix.data(), BYTE_SIZE(lightMatrix), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    _uniforms.cpu = _uniforms.gpu.span<glm::mat4>();

    std::vector<uint32_t> alloc(_numCascades, ~0u);
    debugBuffer = device().createCpuVisibleBuffer(alloc.data(), BYTE_SIZE(alloc), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    viewIndexes = debugBuffer.span<uint32_t>();
    device().setName<VK_OBJECT_TYPE_BUFFER>("shadow_map_cascade_matrices", _uniforms.gpu.buffer);
}

void CascadeShadowMap::createDescriptorSetLayouts() {
    _descriptorSetLayout =
        device().descriptorSetLayoutBuilder()
            .name("cascade_shadow_map_light_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT)
        .createLayout();

    _meshDescriptorSetLayout =
        device().descriptorSetLayoutBuilder()
            .name("cascade_shadow_map_mesh_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL_GRAPHICS)
        .createLayout();
}

void CascadeShadowMap::updateDescriptorSets() {
    _descriptorSet = _descriptorPool->allocate({ _descriptorSetLayout }).front();

    auto writes = initializers::writeDescriptorSets<3>();
    writes[0].dstSet = _descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo info{ _uniforms.gpu, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &info;

    writes[1].dstSet = _descriptorSet;
    writes[1].dstBinding = 2;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo debuginfo{ debugBuffer, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &debuginfo;

    writes[2].dstSet = _descriptorSet;
    writes[2].dstBinding = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo imageInfo{
            _shadowMap[0].sampler.handle
            , _shadowMap[0].imageView.handle
            , VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[2].pImageInfo = &imageInfo;

    device().updateDescriptorSets(writes);
}

void CascadeShadowMap::createPipeline() {
    _pipeline =
        device().graphicsPipelineBuilder()
            .allowDerivatives()
            .shaderStage()
                .vertexShader(FileManager::resource("gltf_cascade_shadow_map.vert.spv"))
                .fragmentShader(FileManager::resource("cascade_shadow_map.frag.spv"))
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
                .enableDepthBias()
                .depthBiasConstantFactor(_depthBiasConstant)
                .depthBiasSlopeFactor(_depthBiasSlope)
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
                .depthAttachment(_depthFormat)
                .viewMask(viewMask())
            .subpass(0)
            .name("cascade_shadow_map")
        .build(_layout);

    if(_renderPass) {
        debug.pipeline =
            device().graphicsPipelineBuilder()
                .shaderStage()
                    .vertexShader(FileManager::resource("quad.vert.spv"))
                    .fragmentShader(FileManager::resource("cascade_shadow_map_debug.frag.spv"))
                    .vertexInputState()
                        .addVertexBindingDescriptions(ClipSpace::bindingDescription())
                        .addVertexAttributeDescriptions(ClipSpace::attributeDescriptions())
                    .inputAssemblyState()
                        .triangleStrip()
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
                        .cullBackFace()
                        .frontFaceCounterClockwise()
                        .polygonModeFill()
                    .depthStencilState()
                        .enableDepthWrite()
                        .enableDepthTest()
                        .compareOpAlways()
                        .minDepthBounds(0)
                        .maxDepthBounds(1)
                    .colorBlendState()
                        .attachment()
                        .add()
                    .layout()
                        .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint32_t))
                        .addDescriptorSetLayout(_descriptorSetLayout)
                    .renderPass(_renderPass->renderPass)
                    .subpass(0)
                .name("debug_cascade_shadow_map")
            .build(debug.layout);
    }
}

uint32_t CascadeShadowMap::viewMask() const {
//    return (1u << _numCascades) - 1;
    return 0;
}

VulkanDevice &CascadeShadowMap::device() {
    return *_device;
}

uint32_t CascadeShadowMap::cascadeCount() const {
    return _numCascades;
}

void CascadeShadowMap::render(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debug.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debug.layout.handle, 0, 1, &_descriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, debug.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint32_t), &_numCascades);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void CascadeShadowMap::splitLambda(float value) {
    _splitLambda = value;
}

VulkanBuffer CascadeShadowMap::cascadeViewProjection() const {
    return _uniforms.gpu;
}

VulkanDescriptorSetLayout CascadeShadowMap::descriptorSetLayout() const {
    return _descriptorSetLayout;
}

VkDescriptorSet CascadeShadowMap::descriptorSet() const {
    return _descriptorSet;
}