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

    _uniformDescriptorSetLayout =
        _device->descriptorSetLayoutBuilder()
            .name("default_uniform_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();
}

void AppContext::updateDescriptorSets() {
    auto sets = _descriptorPool->allocate( { _instanceSetLayout, _uniformDescriptorSetLayout  });
    _defaultInstanceSet = sets[0];
    _atmosphere.info.descriptorSet = sets[1];

    auto writes = initializers::writeDescriptorSets<2>();
    
    writes[0].dstSet = _defaultInstanceSet;
    writes[0].dstBinding = 0 ;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo instanceInfo{ _instanceTransforms, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &instanceInfo;

    writes[1].dstSet = _atmosphere.info.descriptorSet;
    writes[1].dstBinding = 0 ;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo atmospheresInfo{ _atmosphere.info.gpu, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &atmospheresInfo;

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
    FileManager::instance().addSearchPath("common/spv");
    FileManager::instance().addSearchPath("common/data");
    initPrototype();
    glm::mat4 model{1};
    _instanceTransforms = _device->createDeviceLocalBuffer(glm::value_ptr(model), sizeof(model), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    initAtmosphere();

    createDescriptorSets();
    updateDescriptorSets();

    auto clipQuad = ClipSpace::Quad::positions;
    _clipSpaceBuffer  = _device->createDeviceLocalBuffer(clipQuad.data(), BYTE_SIZE(clipQuad), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    createPipelines();
    initFloor();
}

void AppContext::initAtmosphere() {
    _atmosphere.descriptor = AtmosphereDescriptor{ &device(), &descriptorPool() };
    AtmosphereInfo defaultInfo{};
    _atmosphere.descriptor.init();
    _atmosphere.descriptor.load(resource("default.atmosphere"));
    _atmosphere.info.gpu = _device->createCpuVisibleBuffer(&defaultInfo, sizeof(defaultInfo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    _atmosphere.info.cpu = reinterpret_cast<AtmosphereInfo*>(_atmosphere.info.gpu.map());
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
    auto solidBuilder = _prototype->cloneGraphicsPipeline();
    _shading.solid.pipeline =
        solidBuilder
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
			.name("solid_renderer")
			.build(_shading.solid.layout);

    _shading.dynamic.solid.pipeline =
        solidBuilder
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .depthAttachment(VK_FORMAT_D16_UNORM)
            .name("dynamic_solid_renderer")
        .build(_shading.dynamic.solid.layout);

    _shading.flat.pipeline =
        _prototype->cloneGraphicsPipeline()
			.shaderStage()
				.vertexShader(resource("flat.vert.spv"))
                .fragmentShader(resource("flat.frag.spv"))
            .inputAssemblyState()
                .triangles()
            .dynamicState()
                .primitiveTopology()
                .polygonModeEnable()
                .cullMode()
            .layout()
                .addDescriptorSetLayout(AppContext::instanceSetLayout())
			.name("flat_render")
			.build(_shading.flat.layout);
			
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

    _shading.atmosphere.pipeline =
        _prototype->cloneScreenSpaceGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("atmosphere.vert.spv"))
                .fragmentShader(resource("atmosphere.frag.spv"))
            .depthStencilState()
                .compareOpLessOrEqual()
            .layout()
                .addDescriptorSetLayout(_uniformDescriptorSetLayout)
                .addDescriptorSetLayout(_atmosphere.descriptor.uboDescriptorSetLayout)
                .addDescriptorSetLayout(_atmosphere.descriptor.lutDescriptorSetLayout)
            .name("atmosphere")
        .build(_shading.atmosphere.layout);

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

void AppContext::addImageMemoryBarriers(VkCommandBuffer commandBuffer, const std::vector<std::reference_wrapper<VulkanImage>> &images,
                                        VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask) {
    std::vector<VkImageMemoryBarrier> barriers(images.size());

    for(int i = 0; i < images.size(); i++) {
        barriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; // TODO add as param
        barriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;  // TODO add as param
        barriers[i].oldLayout = images[i].get().currentLayout;
        barriers[i].newLayout = images[i].get().currentLayout;
        barriers[i].image = images[i].get();
        barriers[i].subresourceRange = DEFAULT_SUB_RANGE;
    }

    vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, 0, 0,nullptr
            , 0, nullptr, COUNT(barriers), barriers.data());
}

void AppContext::updateSunDirection(glm::vec3 direction) {
    instance._atmosphere.info.cpu->sunDirection = glm::vec4(direction, 1);
}

AppContext AppContext::instance;
