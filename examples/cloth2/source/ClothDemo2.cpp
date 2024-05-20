#include "ClothDemo2.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "VerletIntegrator.hpp"

ClothDemo2::ClothDemo2(const Settings& settings) : VulkanBaseApp("Cloth", settings) {
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/cloth2");
    fileManager.addSearchPathFront("../../examples/cloth2/data");
    fileManager.addSearchPathFront("../../examples/cloth2/spv");
    fileManager.addSearchPathFront("../../examples/cloth2/models");
    fileManager.addSearchPathFront("../../examples/cloth2/textures");
}

void ClothDemo2::initApp() {
    createFloor();
    createGeometry();
    createCloth();
    initCamera();
    createDescriptorPool();
    createDescriptorSetLayouts();
    initIntegrators();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();

}

void ClothDemo2::initIntegrators() {
    integrator = std::make_unique<VerletIntegrator>(device, descriptorPool, cloth, geometry, 960);
    integrator->init();
}

void ClothDemo2::createFloor() {
    auto xform = glm::rotate(glm::mat4{1}, -glm::half_pi<float>(), {1, 0, 0});
    auto plane = primitives::plane(10, 10, 16, 16, xform, {0, 0, 1, 0});

    floor.vertices = device.createDeviceLocalBuffer(plane.vertices.data(), sizeof(Vertex) * plane.vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    floor.indices = device.createDeviceLocalBuffer(plane.indices.data(), sizeof(uint32_t) * plane.indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    floor.indexCount = plane.indices.size();
}

void ClothDemo2::createCloth() {
    cloth = std::make_shared<Cloth>( device );
    cloth->init();
}

void ClothDemo2::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.floorOffset = cloth->size().x * 0.5;
    cameraSettings.velocity = glm::vec3{5};
    cameraSettings.acceleration = glm::vec3(5);
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);
    cameraSettings.horizontalFov = true;
    camera = std::make_unique<SpectatorCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera->lookAt({-5, 2, 3}, {0, cloth->size().y * .5, 0}, {0, 1, 0});
}


void ClothDemo2::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 6> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
                    { VK_DESCRIPTOR_TYPE_SAMPLER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets },
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void ClothDemo2::createDescriptorSetLayouts() {
}

void ClothDemo2::updateDescriptorSets(){
}

void ClothDemo2::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void ClothDemo2::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void ClothDemo2::createRenderPipeline() {
    //    @formatter:off
	auto builder = prototypes->cloneGraphicsPipeline();
	render.wireframe.pipeline =
		builder
			.shaderStage()
				.vertexShader(resource("flat.vert.spv"))
				.fragmentShader(resource("flat.frag.spv"))
			.inputAssemblyState()
				.triangleStrip()
			.rasterizationState()
				.cullNone()
				.polygonModeLine()
			.name("wireframe")
			.build(render.wireframe.layout);

    constexpr float pointSize = 5.0;
    auto builder1 = prototypes->cloneGraphicsPipeline();
	render.points.pipeline =
		builder1
			.shaderStage()
				.vertexShader(resource("point.vert.spv"))
                    .addSpecialization(pointSize, 1)
				.fragmentShader(resource("point.frag.spv"))
			.inputAssemblyState()
				.points()
			.rasterizationState()
				.cullNone()
            .depthStencilState()
                .compareOpAlways()
            .layout().clear()
                .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Camera) + sizeof(glm::vec4))
			.name("points")
			.build(render.points.layout);

	auto builder2 = prototypes->cloneGraphicsPipeline();
	render.normals.pipeline =
		builder2
			.shaderStage()
				.vertexShader(resource("draw_normals.vert.spv"))
				.geometryShader(resource("draw_normals.geom.spv"))
                .fragmentShader(resource("point.frag.spv"))
			.inputAssemblyState()
				.points()
			.rasterizationState()
				.cullNone()
				.polygonModeLine()
            .depthStencilState()
                .compareOpAlways()
            .layout().clear()
                .addPushConstantRange(VK_SHADER_STAGE_GEOMETRY_BIT, 0, sizeof(Camera) + sizeof(glm::vec4) + sizeof(float))
			.name("render_normals")
			.build(render.normals.layout);
    //    @formatter:on
}


void ClothDemo2::onSwapChainDispose() {
    dispose(render.wireframe.pipeline);
}

void ClothDemo2::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *ClothDemo2::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    static std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = {0, 0, 0, 1};
    clearValues[1].depthStencil = {1.0, 0u};

    VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
    rPassInfo.clearValueCount = COUNT(clearValues);
    rPassInfo.pClearValues = clearValues.data();
    rPassInfo.framebuffer = framebuffers[imageIndex];
    rPassInfo.renderArea.offset = {0u, 0u};
    rPassInfo.renderArea.extent = swapChain.extent;
    rPassInfo.renderPass = renderPass;

    vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    renderGeometry(commandBuffer);
    renderCloth(commandBuffer);
    renderFloor(commandBuffer);
    renderPoints(commandBuffer);
    renderNormals(commandBuffer);

    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    integrator->integrate(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void ClothDemo2::renderFloor(VkCommandBuffer commandBuffer) {
    static glm::mat4 identity{1};
    VkDeviceSize offset = 0;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.wireframe.pipeline.handle);
    camera->push(commandBuffer, render.wireframe.layout, identity);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, floor.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, floor.indices, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, floor.indexCount, 1, 0, 0, 0);
}

void ClothDemo2::renderCloth(VkCommandBuffer commandBuffer) {
    static glm::mat4 identity{1};

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.wireframe.pipeline.handle);
    camera->push(commandBuffer, render.wireframe.layout, identity);
    cloth->bindVertexBuffers(commandBuffer);
    vkCmdDrawIndexed(commandBuffer, cloth->indexCount(), 1, 0, 0, 0);
}

void ClothDemo2::renderGeometry(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.wireframe.pipeline.handle);
    camera->push(commandBuffer, render.wireframe.layout, geometry->ubo.worldSpaceXform);
    geometry->bindVertexBuffers(commandBuffer);
    vkCmdDrawIndexed(commandBuffer, geometry->indexCount, 1, 0, 0, 0);
}

void ClothDemo2::renderPoints(VkCommandBuffer commandBuffer) {
    if(!showPoints) return;

    static glm::vec4 pointColor{1, 0, 0, 1};

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.points.pipeline.handle);
    camera->push(commandBuffer, render.points.layout, glm::mat4{1});
    vkCmdPushConstants(commandBuffer, render.points.layout.handle, VK_SHADER_STAGE_VERTEX_BIT,
                       sizeof(Camera),sizeof(glm::vec4), &pointColor.r);

    cloth->bindVertexBuffers(commandBuffer);
    vkCmdDraw(commandBuffer, cloth->vertexCount(), 1, 0, 0);
}

void ClothDemo2::renderNormals(VkCommandBuffer commandBuffer) {
    if(!showNormals) return;

    static glm::vec4 normalColor{1, 1, 0, 1};
    static float normalLength =  0.5f/glm::length(cloth->size());
    static std::array<char, sizeof(normalColor) + sizeof(normalLength)> normalConstants{};

    std::memcpy(normalConstants.data(), &normalColor[0], sizeof(normalColor));
    std::memcpy(normalConstants.data() + sizeof(normalColor), &normalLength, sizeof(normalLength));
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.normals.pipeline.handle);
    camera->push(commandBuffer, render.normals.layout, glm::mat4{1},VK_SHADER_STAGE_GEOMETRY_BIT);
    vkCmdPushConstants(commandBuffer, render.normals.layout.handle, VK_SHADER_STAGE_GEOMETRY_BIT, sizeof(Camera),
                       normalConstants.size(), normalConstants.data());

    cloth->bindVertexBuffers(commandBuffer);
    vkCmdDraw(commandBuffer, cloth->vertexCount(), 1, 0, 0);
}

void ClothDemo2::renderUI(VkCommandBuffer commandBuffer) {
    auto& imGuiPlugin = plugin<ImGuiPlugin>(IM_GUI_PLUGIN);

    ImGui::Begin("Cloth Simulation");
    ImGui::SetWindowSize("Cloth Simulation", {0, 0});
    static int option = 0;

    ImGui::RadioButton("wireframe", &option, 0);
    ImGui::SameLine();
    ImGui::RadioButton("shaded", &option, 1);
    shading = static_cast<Shading>(option);

    bool showWireframeOptions = shading == Shading::WIREFRAME;
    if(ImGui::CollapsingHeader("wireframe options", &showWireframeOptions, ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::Checkbox("points", &showPoints);
        ImGui::Checkbox("normals", &showNormals);
    }

    static bool wind = integrator->constants.simWind;
    ImGui::Checkbox("wind", &wind);
    integrator->constants.simWind = wind;

//    ImGui::SliderFloat("shine", &shine, 1, 100);
//    ImGui::Text("%d iteration(s), timeStep: %.3f ms", numIterations, frameTime * 1000);
//    ImGui::Text("Application average %.3f ms/frame, (%d FPS)", 1000.0/framePerSecond, framePerSecond);
//    ImGui::Text("compute: %.3f ms", computeDuration);
//    ImGui::Text(fmt::format("Camera position: {}, target: {}", cameraController->position(), cameraController->target).c_str());
    if(ImGui::Button("run") && !simRunning){
        simRunning = true;
    }
    ImGui::End();

    imGuiPlugin.draw(commandBuffer);
}

void ClothDemo2::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }
    auto cam = camera->cam();
    glfwSetWindowTitle(window, fmt::format("{}, vertex count {}, fps {}", title, cloth->vertexCount(), framePerSecond).c_str());

    if(simRunning) {
        integrator->update(time);
    }

}

void ClothDemo2::checkAppInputs() {
    camera->processInput();
}

void ClothDemo2::cleanup() {
    VulkanBaseApp::cleanup();
}

void ClothDemo2::onPause() {
    VulkanBaseApp::onPause();
}

void ClothDemo2::createGeometry() {
    geometry = std::make_shared<Geometry>();
    auto& worldSpaceXform = geometry->ubo.worldSpaceXform;
    const auto r = geometry->radius;
    worldSpaceXform = glm::scale(worldSpaceXform, glm::vec3(r * 2, r, r));
    worldSpaceXform = glm::translate(worldSpaceXform, {0, geometry->radius, 0});
    geometry->ubo.localSpaceXform = glm::inverse(worldSpaceXform);
    geometry->ubo.radius = geometry->radius;
    geometry->ubo.center = (worldSpaceXform * glm::vec4(0, 0, 0, 1)).xyz();
    auto localSapce = geometry->ubo.localSpaceXform;

    auto s = primitives::sphere(25, 25, 1.0, glm::mat4(1),  {1, 1, 0, 1});
    geometry->vertices = device.createDeviceLocalBuffer(s.vertices.data(), sizeof(Vertex) * s.vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    geometry->indices = device.createDeviceLocalBuffer(s.indices.data(), sizeof(uint32_t) * s.indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    geometry->indexCount = s.indices.size();

    geometry->uboBuffer = device.createDeviceLocalBuffer(&geometry->ubo, sizeof(geometry->ubo), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    spdlog::error("geom size: {}", sizeof(geometry->ubo));
}


int main(){
    try{

        Settings settings;
        settings.width = settings.height = 1024;
        settings.depthTest = true;
        settings.enableBindlessDescriptors = false;
        settings.enabledFeatures.fillModeNonSolid = true;
        settings.enabledFeatures.geometryShader = true;
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();

        auto app = ClothDemo2{settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}