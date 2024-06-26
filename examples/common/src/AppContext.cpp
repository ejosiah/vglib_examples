#include "AppContext.hpp"
#include "FileManager.hpp"
#include "Vertex.h"

AppContext::AppContext(VulkanDevice &device, VulkanDescriptorPool& descriptorPool, VulkanSwapChain& swapChain, VulkanRenderPass& renderPass)
:_device{ &device}
, _descriptorPool{ &descriptorPool}
, _swapChain{ &swapChain }
, _renderPass{ &renderPass }
{}


void AppContext::init(VulkanDevice &device, VulkanDescriptorPool &descriptorPool, VulkanSwapChain& swapChain, VulkanRenderPass& renderPass) {
    instance = AppContext{ device, descriptorPool, swapChain, renderPass};
    instance.init0();
}

VulkanDevice& AppContext::device() {
    return *instance._device;
}

VulkanDescriptorPool& AppContext::descriptorPool() {
    return *instance._descriptorPool;
}

void AppContext::createDescriptorSets() {
    _instanceSetLayout =
        _device->descriptorSetLayoutBuilder()
            .name("default_instance_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();

    _defaultInstanceSet = _descriptorPool->allocate( { _instanceSetLayout  }).front();
}

void AppContext::updateDescriptorSets() {
    auto writes = initializers::writeDescriptorSets<1>();
    
    writes[0].dstSet = instance._defaultInstanceSet;
    writes[0].dstBinding = 0 ;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo instanceInfo{ instance._instanceTransforms, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &instanceInfo;

    _device->updateDescriptorSets(writes);
}

void AppContext::bindInstanceDescriptorSets(VkCommandBuffer commandBuffer, VulkanPipelineLayout& layout) {
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout.handle, 0, 1, &instance._defaultInstanceSet, 0, 0);

}

VulkanDescriptorSetLayout &AppContext::instanceSetLayout() {
    return instance._instanceSetLayout;
}


VkDescriptorSet AppContext::allocateInstanceDescriptorSet() {
    return instance._descriptorPool->allocate( { instance._instanceSetLayout  }).front();
}

void AppContext::renderClipSpaceQuad(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, instance._clipSpaceBuffer, &offset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void AppContext::init0() {
    FileManager::instance().addSearchPath("../../examples/common/spv");
    FileManager::instance().addSearchPath("../../examples/common/data");
    initPrototype();
    glm::mat4 model{1};
    _instanceTransforms = _device->createDeviceLocalBuffer(glm::value_ptr(model), sizeof(model), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    createDescriptorSets();
    updateDescriptorSets();

    auto clipQuad = ClipSpace::Quad::positions;
    _clipSpaceBuffer  = _device->createDeviceLocalBuffer(clipQuad.data(), BYTE_SIZE(clipQuad), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    createPipelines();
    initFloor();
}

void AppContext::initPrototype() {
    _prototype = std::make_unique<Prototypes>(*_device, *_swapChain, *_renderPass);
}

void AppContext::shutdown() {
    auto localInstance = std::move(instance);
}

std::vector<VkDescriptorSet> AppContext::allocateDescriptorSets(const std::vector<VulkanDescriptorSetLayout>& setLayouts) {
    return  instance._descriptorPool->allocate(setLayouts);
}

std::string AppContext::resource(const std::string &name) {
    return FileManager::resource(name);
}

void AppContext::createPipelines() {
    _shading.solid.pipeline =
        _prototype->cloneGraphicsPipeline()
			.shaderStage()
				.vertexShader(resource("solid.vert.spv"))
                .fragmentShader(resource("solid.frag.spv"))
            .inputAssemblyState()
                .triangles()
            .dynamicState()
                .primitiveTopology()
                .polygonModeEnable()
                .cullMode()
            .layout()
                .addDescriptorSetLayout(AppContext::instanceSetLayout())
			.name("solid_render")
			.build(_shading.solid.layout);
			
	_shading.wireframe.pipeline =
        _prototype->cloneGraphicsPipeline()
			.shaderStage()
				.vertexShader(resource("wireframe.vert.spv"))
				.fragmentShader(resource("wireframe.frag.spv"))
			.inputAssemblyState()
				.triangles()
			.rasterizationState()
				.cullNone()
				.polygonModeLine()
            .dynamicState()
                .primitiveTopology()
            .layout()
                .addDescriptorSetLayout(AppContext::instanceSetLayout())
                .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(Camera), sizeof(glm::vec3))
			.name("wireframe")
			.build(_shading.wireframe.layout);

}

void AppContext::onResize(VulkanSwapChain &swapChain, VulkanRenderPass &renderPass) {
    instance.init0();
}

void AppContext::initFloor() {
    _floor = Floor{ *_device, *_prototype };
    _floor.init();
}

void AppContext::renderFloor(VkCommandBuffer commandBuffer, BaseCameraController &camera) {
    instance._floor.render(commandBuffer, camera);
}

AppContext AppContext::instance;
