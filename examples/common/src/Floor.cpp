#include "Floor.hpp"
#include "FileManager.hpp"

Floor::Floor(VulkanDevice &device, Prototypes& prototypes)
: _device( &device )
, _prototypes( &prototypes )
{}

void Floor::init() {
    createVertexBuffer();
    createPipeline();
}

void Floor::createPipeline() {
	auto builder = _prototypes->cloneGraphicsPipeline();
	_pipeline =
        builder
			.shaderStage()
				.vertexShader(FileManager::resource("floor.vert.spv"))
                .fragmentShader(FileManager::resource("floor.frag.spv"))
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
			.name("render_floor")
			.build(_layout);
}

void Floor::render(VkCommandBuffer commandBuffer, BaseCameraController& camera) {
    static glm::mat4 identity{1};
    VkDeviceSize offset = 0;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline.handle);
    camera.push(commandBuffer, _layout, identity, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, _vertices, &offset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void Floor::createVertexBuffer() {
    auto vertices = ClipSpace::Quad::positions;
    _vertices  = _device->createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}
