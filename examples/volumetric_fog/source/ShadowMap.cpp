#include "ShadowMap.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "Vertex.h"
#include "xforms.h"


ShadowMap::ShadowMap(FileManager* fileManager, VulkanDevice *device, VulkanDescriptorPool *descriptorPool, Scene* scene)
: m_filemanager{ fileManager}
, m_device{ device }
, m_pool{ descriptorPool}
, m_scene{ scene }
{}

void ShadowMap::init() {
    createTextures();
    initLightSpaceData();
    createDescriptorSet();
    updateDescriptorSet();
    createPipeline();
}

void ShadowMap::createTextures() {

    imageSpec = initializers::imageCreateInfo(
            VK_IMAGE_TYPE_2D,
            VK_FORMAT_D16_UNORM,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            SHADOW_MAP_SIZE,
            SHADOW_MAP_SIZE);

    VkImageSubresourceRange subresourceRange = initializers::imageSubresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT);
    m_texture.image = m_device->createImage(imageSpec);
    m_texture.image.transitionLayout(m_device->graphicsCommandPool(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);

    m_texture.imageView = m_texture.image.createView(imageSpec.format, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    m_texture.sampler = m_device->createSampler(samplerInfo);

}

void ShadowMap::initLightSpaceData() {
    lightSpace.gpu = m_device->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(LightSpaceData), "light_space_xforms");
    lightSpace.cpu = reinterpret_cast<LightSpaceData*>(lightSpace.gpu.map());
}

void ShadowMap::createDescriptorSet() {
    lightMatrixDescriptorSetLayout =
        m_device->descriptorSetLayoutBuilder()
            .name("light_matrix_setLayout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();

    shadowMapDescriptorSetLayout =
        m_device->descriptorSetLayoutBuilder()
            .name("shadow_map_setLayout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();

    auto sets = m_pool->allocate({ lightMatrixDescriptorSetLayout, shadowMapDescriptorSetLayout});
    lightMatrixDescriptorSet = sets[0];
    shadowMapDescriptorSet = sets[1];
}

void ShadowMap::updateDescriptorSet() {
    auto writes = initializers::writeDescriptorSets<2>();
    
    writes[0].dstSet = lightMatrixDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo info{ lightSpace.gpu, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &info;
    
    writes[1].dstSet = shadowMapDescriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo imageInfo{m_texture.sampler.handle, m_texture.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[1].pImageInfo = &imageInfo;
    
    m_device->updateDescriptorSets(writes);
}

void ShadowMap::createPipeline() {
	m_pipeline.pipeline =
		m_device->graphicsPipelineBuilder()
			.shaderStage()
				.vertexShader(resource("shadowmap.vert.spv"))
				.fragmentShader(resource("shadowmap.frag.spv"))
			.vertexInputState()
				.addVertexBindingDescriptions(Vertex::bindingDisc())
                .addVertexAttributeDescriptions(Vertex::attributeDisc())
			.inputAssemblyState()
				.triangles()
			.viewportState()
				.viewport()
					.origin(0, 0)
					.dimension(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE)
					.minDepth(0)
					.maxDepth(1)
				.scissor()
					.offset(0, 0)
					.extent(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE)
				.add()
			.rasterizationState()
				.enableDepthBias()
				.depthBiasConstantFactor(depthBiasConstant)
				.depthBiasSlopeFactor(depthBiasSlope)
				.cullBackFace()
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
				.addDescriptorSetLayout(lightMatrixDescriptorSetLayout)
            .dynamicRenderPass()
                .depthAttachment(imageSpec.format)
			.subpass(0)
			.name("shadow_map")
		.build(m_pipeline.layout);
}

void ShadowMap::generate(CaptureScene &&captureScene) {
    calculateFittingFrustum();

    m_device->graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        info.flags = 0;
        info.renderArea = {{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}};
        info.layerCount = 1;

        VkRenderingAttachmentInfo attachmentInfo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        attachmentInfo.imageView = m_texture.imageView.handle;
        attachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        attachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
        attachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentInfo.clearValue.depthStencil = {1, 0u};
        info.pDepthAttachment = &attachmentInfo;

        vkCmdBeginRendering(commandBuffer, &info);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.layout.handle, 0, 1, &lightMatrixDescriptorSet, 0, nullptr);
        captureScene(commandBuffer);

        vkCmdEndRendering(commandBuffer);
    });
}

void ShadowMap::calculateFittingFrustum() {
//    auto camViewToWorldSpace = glm::inverse(m_scene->camera->proj * m_scene->camera->view);
//
//    // step 1: calculate frustum corners
//    auto frustum = Ndc::points;
//
//    // step 2: transform from view to world space
//    for(auto& point : frustum) {
//        point = camViewToWorldSpace * point;
//        point /= point.w;
//    }
//
//    // step 3: transform from world to light space
//    glm::vec3 lightPos = std::get<1>(m_scene->bounds).y * 2.f * m_scene->cpu->sunDirection;
//    auto target = lightPos - m_scene->cpu->sunDirection;
//    glm::mat4 lightViewSpace = glm::lookAt(lightPos, target, {0, 1, 0});
//
//    auto frustumInLightSpace = frustum;
//    for(auto& point : frustumInLightSpace) {
//        point = lightViewSpace * point;
//    }
//
//    // step 4: calculate axis aligned bounding box in light space
//    glm::vec3 minBounds{MAX_FLOAT};
//    glm::vec3 maxBounds{MIN_FLOAT};
//
//    for(auto point : frustumInLightSpace) {
//        minBounds = glm::min(point.xyz(), minBounds);
//        maxBounds = glm::max(point.xyz(), maxBounds);
//    }
//
//    // step 5: calculate light position ( find the center point of the nearZ plane on bounding box)
//    lightPos = (minBounds + glm::vec3(maxBounds.x, maxBounds.y, minBounds.z)) * .5f;
//
//    // step 6: transform light position to world space
//    lightPos = (glm::inverse(lightViewSpace) * glm::vec4(lightPos, 1)).xyz();
//
//    // step 7: transform the view frustum from world to light space
//    lightViewSpace = glm::lookAt(lightPos, lightPos - m_scene->cpu->sunDirection, {0, 1, 0});
//    frustumInLightSpace = frustum;
//    for(auto& point : frustumInLightSpace) {
//        point = lightViewSpace * point;
//    }
//
//    // step 8: calculate final bounding box in light space
//    minBounds = glm::vec3{MAX_FLOAT};
//    maxBounds = glm::vec3{MIN_FLOAT};
//
//    for(auto point : frustumInLightSpace) {
//        minBounds = glm::min(point.xyz(), minBounds);
//        maxBounds = glm::max(point.xyz(), maxBounds);
//    }
//
//    auto lightProjection = vkn::ortho(minBounds.x, maxBounds.x, minBounds.y, maxBounds.y, minBounds.z, maxBounds.z);
//    lightSpace.cpu->viewProj = lightProjection * lightViewSpace;

    glm::vec3 lightPos = std::get<1>(m_scene->bounds).y * 2.f * m_scene->cpu->sunDirection;
    auto target = lightPos - m_scene->cpu->sunDirection;
    glm::mat4 lightViewSpace = glm::lookAt(lightPos, target, {0, 1, 0});

    constexpr int MIN = 0;
    constexpr int MAX = 1;
    std::array<glm::vec3, 2> bounds{{ glm::vec3(-1), glm::vec3(1) }};
    auto [minBounds, maxBounds] = m_scene->bounds;
    auto diagonal = maxBounds - minBounds;
    auto center = (minBounds + maxBounds) * .5f;
    auto scale = glm::length(diagonal);

    auto xform = glm::translate(glm::mat4{1}, center);
    xform = glm::scale(xform , glm::vec3(scale));

    for(auto& point : bounds) {
        point = (xform * glm::vec4(point, 1)).xyz();
    }

    auto lightProjection = vkn::ortho(bounds[MIN].x, bounds[MAX].x, bounds[MIN].y, bounds[MAX].y, bounds[MIN].z, bounds[MAX].z);
    lightSpace.cpu->viewProj = lightProjection * lightViewSpace;

}

std::string ShadowMap::resource(const std::string &name) {
    auto res = m_filemanager->getFullPath(name);
    assert(res.has_value());
    return res->string();
}
