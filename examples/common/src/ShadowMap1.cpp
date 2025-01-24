#include "ShadowMap1.hpp"
#include "AppContext.hpp"
#include "primitives.h"
#include "VulkanInitializers.h"
#include "filemanager.hpp"
#include "Vertex.h"
#include "Barrier.hpp"


OminiDirectionalShadowMap::OminiDirectionalShadowMap(
        VulkanDevice& device,
        glm::vec3 lightPosition,
        VulkanDescriptorSetLayout meshSetLayout,
        VkDescriptorSet meshSet,
        uint32_t size)
: _device{&device}
, _constants{ .lightPosition = lightPosition }
, _size{ size }
, _mesh{ .descriptorSetLayout = meshSetLayout, .descriptorSet = meshSet  }
{}

void OminiDirectionalShadowMap::init() {
    createCube();
    createShadowMapTexture();
    initLightSpaceData();
    createDescriptorSetLayout();
    updateDescriptorSet();
    createPipeline();
}

void OminiDirectionalShadowMap::update(const glm::vec3 &lightPosition) {
    _constants.lightPosition = lightPosition;

    _constants.farPlane = 1000.f;
    _constants.projection = glm::perspective(glm::half_pi<float>(), 1.0f, 0.1f, _constants.farPlane);
    for(auto i = 0; i < 6; ++i) {
        const auto& face = Faces[i];
        auto view = glm::lookAt(lightPosition, lightPosition + face.direction, face.up);
        _cameras.cpu[i] = view;
    }
}

void OminiDirectionalShadowMap::capture(Scene&& scene, VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 1> sets;
    sets[0] = _mesh.descriptorSet;

//    vkCmdUpdateBuffer(commandBuffer, _cameras.gpu, 0, _cameras.gpu.size, _cameras.cpu.data());
//    Barrier::transferWriteToFragmentRead(commandBuffer, _cameras.gpu);

    static VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    info.flags = 0;
    info.renderArea = {{0, 0}, {_size, _size}};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;

    static VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {1.0f, 1.0f, 1.0f, 1.0f};
    info.pColorAttachments = &colorAttachment;

    static VkRenderingAttachmentInfo attachmentInfo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    attachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    attachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
    attachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachmentInfo.clearValue.depthStencil = {1.0f, 0u};

    info.pDepthAttachment = &attachmentInfo;

    for(auto i = 0; i < 6; ++i){
        colorAttachment.imageView = _transViews[i].handle;
        attachmentInfo.imageView = _views[i].handle;

        vkCmdBeginRendering(commandBuffer, &info);
        _constants.view = _cameras.cpu[i];
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline.pipeline.handle);
//        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdPushConstants(commandBuffer, _pipeline.layout.handle, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(_constants), &_constants);

        vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_BACK_BIT);
        vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        scene();
        vkCmdEndRendering(commandBuffer);
    }
}

void OminiDirectionalShadowMap::modelTransform(VkCommandBuffer commandBuffer, const glm::mat4 &matrix) {
    auto constants = _constants;
    constants.model = matrix;
    vkCmdPushConstants(commandBuffer, _pipeline.layout.handle, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, sizeof(constants), &constants);
}

const Texture& OminiDirectionalShadowMap::depth() const {
    return _depth;
}
const Texture& OminiDirectionalShadowMap::transmission() const {
    return _transmission;
}

void OminiDirectionalShadowMap::createShadowMapTexture() {
    auto format = VK_FORMAT_D16_UNORM;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = { _size, _size, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 6;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    auto& device = AppContext::device();
    _depth.image = device.createImage(imageInfo);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    _depth.sampler = device.createSampler(samplerInfo);
    _transmission.sampler = device.createSampler(samplerInfo);

    VkImageSubresourceRange resourceRange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 6};
    _depth.imageView = _depth.image.createView(format, VK_IMAGE_VIEW_TYPE_CUBE, resourceRange);
    for(auto i = 0; i < 6; i++) {
        resourceRange.baseArrayLayer = i;
        resourceRange.layerCount = 1;
        _views[i] = _depth.image.createView(format, VK_IMAGE_VIEW_TYPE_2D, resourceRange);
    }

    _depth.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, resourceRange);
    _depth.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, resourceRange);


    format = VK_FORMAT_R32G32B32A32_SFLOAT;
    imageInfo.format = format;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    _transmission.image = device.createImage(imageInfo);

    resourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    _transmission.imageView = _transmission.image.createView(format, VK_IMAGE_VIEW_TYPE_2D, resourceRange);
    for(auto i = 0; i < 6; i++) {
        resourceRange.baseArrayLayer = i;
        resourceRange.layerCount = 1;
        _transViews[i] = _transmission.image.createView(format, VK_IMAGE_VIEW_TYPE_2D, resourceRange);
    }
}

void OminiDirectionalShadowMap::initLightSpaceData() {
    auto& device = AppContext::device();
    update(_constants.lightPosition);
    _cameras.gpu = device.createDeviceLocalBuffer(_cameras.cpu.data(), BYTE_SIZE(_cameras.cpu), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
}

void OminiDirectionalShadowMap::createCube() {
    auto geom = primitives::cube({1, 0, 0, 1});
    std::vector<glm::vec3> vertices;
    for(const auto& vertex : geom.vertices){
        vertices.push_back(vertex.position.xyz());
    }

    auto& device = AppContext::device();
    _cube.vertices = device.createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    _cube.indices = device.createDeviceLocalBuffer(geom.indices.data(), BYTE_SIZE(geom.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void OminiDirectionalShadowMap::createDescriptorSetLayout() {
    auto& device = AppContext::device();
    
    _descriptorSetLayout = 
        device.descriptorSetLayoutBuilder()
            .name("omini_shadow_map_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT)
            .createLayout();
}

void OminiDirectionalShadowMap::updateDescriptorSet() {
    _descriptorSet = AppContext::descriptorPool().allocate( { _descriptorSetLayout  }).front();
    
    auto writes = initializers::writeDescriptorSets<1>();
    writes[0].dstSet = _descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo info{ _cameras.gpu, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &info;

    AppContext::device().updateDescriptorSets(writes);
}

void OminiDirectionalShadowMap::createPipeline() {
	_pipeline.pipeline =
		_device->graphicsPipelineBuilder()
			.shaderStage()
				.vertexShader(FileManager::resource("point_shadow_map.vert.spv"))
				.fragmentShader(FileManager::resource("point_shadow_map.frag.spv"))
			.vertexInputState()
				.addVertexBindingDescriptions(Vertex::bindingDisc())
                .addVertexAttributeDescriptions(Vertex::attributeDisc())
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
//				.enableDepthBias()
//				.depthBiasConstantFactor(_depthBiasConstant)
//				.depthBiasSlopeFactor(_depthBiasSlope)
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
				.addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(_constants))
//                .addDescriptorSetLayout(_mesh.descriptorSetLayout)
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .depthAttachment(VK_FORMAT_D16_UNORM)
			.subpass(0)
			.name("point_shadow_map")
		.build(_pipeline.layout);
            
}