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
    createDescriptorPool();
    createDescriptorSetLayouts();
    createCloth();
    initCamera();
    loadModel();
    updateDescriptorSets();
    initIntegrators();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
}

void ClothDemo2::initIntegrators() {
    integrator = std::make_unique<VerletIntegrator>(device, descriptorPool, accStructDescriptorSetLayout,
                                                    accStructDescriptorSet, cloth, geometry, 960);
    integrator->init();
}

void ClothDemo2::createFloor() {
    auto xform = glm::rotate(glm::mat4{1}, -glm::half_pi<float>(), {1, 0, 0});
    auto plane = primitives::plane(10, 10, 16, 16, xform, {0, 0, 1, 0});

    auto vertices = ClipSpace::Quad::positions;
    floor.vertices  = device.createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
//    floor.vertices = device.createDeviceLocalBuffer(plane.vertices.data(), sizeof(Vertex) * plane.vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
//    floor.indices = device.createDeviceLocalBuffer(plane.indices.data(), sizeof(uint32_t) * plane.indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
//    floor.indexCount = plane.indices.size();
}

void ClothDemo2::loadModel() {
    std::vector<mesh::Mesh> meshes;
    mesh::load(meshes, resource("cow.ply"));
    for(auto& mesh : meshes) {
        for(auto& vertex : mesh.vertices){
            vertex.color = glm::vec4(1, 1, 0, 1);
        }
    }
//    spdlog::info("bounds [min: {}, max: {}]", meshes.front().bounds.min, meshes.front().bounds.max);

//    auto s = primitives::sphere(25, 25, 1.0, glm::mat4(1),  {1, 1, 0, 1}, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
//    auto s = primitives::hemisphere(25, 25, 1.0,  {1, 1, 0, 1}, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
//    mesh::Mesh mesh;
//    mesh.vertices = s.vertices;
//    mesh.indices = s.indices;
//    meshes.push_back(mesh);

    phong::VulkanDrawableInfo info{};
    info.vertexUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.indexUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.materialUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.materialIdUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.generateMaterialId = true;

    phong::load(device, descriptorPool, model, meshes, info);
    rt::MeshObjectInstance instance{ .object = rt::TriangleMesh{ &model }};

    accStructBuilder = rt::AccelerationStructureBuilder{&device};
    accStructBuilder.add({ instance });
    accStructBuilder.buildTlas(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_DATA_ACCESS_KHR);
}

void ClothDemo2::createCloth() {
    auto materialSets = descriptorPool.allocate( { materialSetLayout, materialSetLayout, materialSetLayout });
    cloth = std::make_shared<Cloth>( device, materialSets );
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
    std::array<VkDescriptorPoolSize, 7> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
                    { VK_DESCRIPTOR_TYPE_SAMPLER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 100 * maxSets}
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void ClothDemo2::createDescriptorSetLayouts() {
    accStructDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();

    materialSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("material_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(4)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .createLayout();

}

void ClothDemo2::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( { accStructDescriptorSetLayout });
    accStructDescriptorSet = sets[0];
    auto accWrites = VkWriteDescriptorSetAccelerationStructureKHR{};
    accWrites.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    accWrites.accelerationStructureCount = 1;
    accWrites.pAccelerationStructures = accStructBuilder.accelerationStructure();

    auto writes = initializers::writeDescriptorSets<4>();
    writes[0].dstSet = accStructDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writes[0].descriptorCount = 1;
    writes[0].pNext = &accWrites;
    
    writes[1].dstSet = accStructDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo vertexInfo{ model.vertexBuffer, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &vertexInfo;

    writes[2].dstSet = accStructDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo indexInfo{ model.indexBuffer, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &indexInfo;

    writes[3].dstSet = accStructDescriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    VkDescriptorBufferInfo offsetInfo{ model.offsetBuffer, 0, VK_WHOLE_SIZE };
    writes[3].pBufferInfo = &offsetInfo;

    device.updateDescriptorSets(writes);
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
            .dynamicState()
                .primitiveTopology()
                .cullMode()
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

	auto builder3 = prototypes->cloneGraphicsPipeline();
	render.floor.pipeline =
        builder3
			.shaderStage()
				.vertexShader(resource("floor.vert.spv"))
                .fragmentShader(resource("floor.frag.spv"))
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
			.build(render.floor.layout);

	auto builder4 = prototypes->cloneGraphicsPipeline();
	render.solid.pipeline =
        builder4
			.shaderStage()
				.vertexShader(resource("solid.vert.spv"))
                .fragmentShader(resource("solid.frag.spv"))
            .inputAssemblyState()
                .triangleStrip()
            .dynamicState()
                .primitiveTopology()
                .cullMode()
			.name("solid_render")
			.build(render.solid.layout);

    render.solidTex.pipeline =
        builder4
            .shaderStage()
                .fragmentShader(resource("solid_texture.frag.spv"))
            .layout()
                .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(Camera), sizeof(int))
                .addDescriptorSetLayout(materialSetLayout)
            .name("solid_render_texture")
            .build(render.solidTex.layout);
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

    renderFloor(commandBuffer);
    renderGeometry(commandBuffer);
//    renderModel(commandBuffer);
    renderCloth(commandBuffer);
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

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.floor.pipeline.handle);
    camera->push(commandBuffer, render.floor.layout, identity, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, floor.vertices, &offset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void ClothDemo2::renderCloth(VkCommandBuffer commandBuffer) {
    static glm::mat4 identity{1};

    if(shading == Shading::WIREFRAME) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.wireframe.pipeline.handle);
        camera->push(commandBuffer, render.wireframe.layout, identity);
    }else {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.solidTex.pipeline.handle);
        cloth->bindMaterial(commandBuffer, render.solidTex.layout.handle, materialId);
        vkCmdPushConstants(commandBuffer, render.solidTex.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(Camera), sizeof(int), &clothColor);
        camera->push(commandBuffer, render.solidTex.layout, identity);
    }

    cloth->bindVertexBuffers(commandBuffer);
    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
    vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_NONE);
    vkCmdDrawIndexed(commandBuffer, cloth->indexCount(), 1, 0, 0, 0);
}

void ClothDemo2::renderGeometry(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.solid.pipeline.handle);
    camera->push(commandBuffer, render.solid.layout, geometry->ubo.worldSpaceXform);
    geometry->bindVertexBuffers(commandBuffer);
    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
    vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_BACK_BIT);
    vkCmdDrawIndexed(commandBuffer, geometry->indexCount, 1, 0, 0, 0);
}

void ClothDemo2::renderModel(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.solid.pipeline.handle);
    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_BACK_BIT);
    camera->push(commandBuffer, render.solid.layout, glm::mat4{1});
    model.draw(commandBuffer);
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
    static int option = static_cast<int>(shading);

    ImGui::RadioButton("wireframe", &option, 0);
    ImGui::SameLine();
    ImGui::RadioButton("shaded", &option, 1);
    shading = static_cast<Shading>(option);

    bool showWireframeOptions = shading == Shading::WIREFRAME;
    if(ImGui::CollapsingHeader("wireframe options", &showWireframeOptions, ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::Checkbox("points", &showPoints);
        ImGui::Checkbox("normals", &showNormals);
    }

    static int numMaterials = cloth->numMaterials();
    ImGui::Text("Material:");
    ImGui::Indent(16);
    static std::array<const char*, 3> matLabel{"Chenille Polyester Upholstery", "Denim", "Bengaline"};
    ImGui::Combo("material", &materialId, matLabel.data(), matLabel.size());

    if(materialId == 0) {
        ImGui::SliderInt("color variation", &clothColor, 0, 2);
    }else{
        clothColor = 0;
    }
    ImGui::Indent(-16);

    static bool wind = integrator->constants.simWind;
    ImGui::Text("Wind:");
    ImGui::Indent(16);
    if(wind){
        ImGui::SliderFloat("strength", &integrator->constants.windStrength, 1, 10);
        ImGui::SliderFloat("speed", &integrator->constants.windSpeed, 0.1, 1);
    }
    ImGui::Checkbox("wind", &wind);
    ImGui::Indent(-16);

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

void ClothDemo2::beforeDeviceCreation() {
    static VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &posFetchFeature };
    vkGetPhysicalDeviceFeatures2(device.physicalDevice, &features);
    if(posFetchFeature.rayTracingPositionFetch) {
        spdlog::info("ray tracing position fetch supported");
        posFetchFeature.pNext = deviceCreateNextChain;
        deviceCreateNextChain = &posFetchFeature;
    }else {
        spdlog::warn("ray tracing position fetch not supported...");
    }
}


int main(){
    try{

        Settings settings;
        settings.width = settings.height = 1024;
        settings.depthTest = true;
        settings.enableBindlessDescriptors = false;
        settings.enabledFeatures.fillModeNonSolid = true;
        settings.enabledFeatures.geometryShader = true;
        settings.deviceExtensions.push_back(VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME);
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();

        auto app = ClothDemo2{settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}