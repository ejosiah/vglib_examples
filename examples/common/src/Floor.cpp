#include "Floor.hpp"
#include "FileManager.hpp"

Floor::Floor(VulkanDevice& device,
             Prototypes& prototypes,
             const std::optional<std::string>& vertex,
             const std::optional<std::string>& fragment,
             const std::vector<VulkanDescriptorSetLayout> descriptorSetLayouts)
: _device( &device )
, _prototypes( &prototypes )
, _vertexShader{ vertex }
, _fragmentShader{ fragment }
, _descriptorSetLayouts{ descriptorSetLayouts }
{}

void Floor::init() {
    createVertexBuffer();
    createPipeline();
}

void Floor::createPipeline() {
    const auto vertexShader = _vertexShader.value_or(FileManager::resource("floor.vert.spv"));
    const auto fragmentShader = _fragmentShader.value_or(FileManager::resource("floor.frag.spv"));

	auto builder = _prototypes->cloneGraphicsPipeline();
	_pipeline =
        builder
			.shaderStage()
				.vertexShader(vertexShader)
                .fragmentShader(fragmentShader)
            .vertexInputState().clear()
                .addVertexBindingDescriptions(ClipSpace::bindingDescription())
                .addVertexAttributeDescriptions(ClipSpace::attributeDescriptions())
            .inputAssemblyState()
                .triangleStrip()
			.rasterizationState()
				.cullNone()
            .depthStencilState()
                .compareOpLessOrEqual()
            .layout().clear()
                .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Camera))
                .addDescriptorSetLayouts(_descriptorSetLayouts)
			.name("render_floor")
			.build(_layout);
}

void Floor::render(VkCommandBuffer commandBuffer, BaseCameraController& camera, const std::vector<VkDescriptorSet> descriptorSets) {
    static glm::mat4 identity{1};
    VkDeviceSize offset = 0;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline.handle);

    if(!descriptorSets.empty()){
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _layout.handle, 0, descriptorSets.size(), descriptorSets.data(), 0, VK_NULL_HANDLE);
    }

    camera.push(commandBuffer, _layout, identity, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, _vertices, &offset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void Floor::createVertexBuffer() {
    auto vertices = ClipSpace::Quad::positions;
    _vertices  = _device->createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}
