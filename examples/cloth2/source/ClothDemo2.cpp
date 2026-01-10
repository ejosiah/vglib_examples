#include "ClothDemo2.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "VerletSolver.hpp"

ClothDemo2::ClothDemo2(const Settings& settings) : VulkanBaseApp("Cloth", settings) {
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("../data/cloth");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("common/spv");
    fileManager().addSearchPathFront("cloth2");
    fileManager().addSearchPathFront("cloth2/spv");
    fileManager().addSearchPathFront("cloth2/textures");
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
    initSolvers();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    dummyBuffer = device.createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(glm::vec3), "dummy_buffer");
}

void ClothDemo2::initSolvers() {
    verletSolver = std::make_unique<VerletSolver>(device, descriptorPool, accStructDescriptorSetLayout,
                                                  accStructDescriptorSet, cloth, geometry, 60);

    pbdSolver = std::make_unique<PBDSolver>(device, descriptorPool, accStructDescriptorSetLayout,
                                            accStructDescriptorSet, cloth, geometry, 60);
    verletSolver->init();
    pbdSolver->init();

    positions[0][0] = verletSolver->position(0);
    positions[0][1] = verletSolver->position(1);
    positions[1][0] = pbdSolver->position(0);
    positions[1][1] = pbdSolver->position(1);

    solver = solverType == 0 ? verletSolver.get() : pbdSolver.get();
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
//    mesh::normalize(meshes, 2.5);
    for(auto& mesh : meshes) {
        for(auto& vertex : mesh.vertices){
            vertex.position = glm::vec4{vertex.position.xyz() * 1.04f, 1};
            vertex.color = glm::vec4(1, 1, 0, 1);
        }
    }

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
    auto materialSets = descriptorPool.allocate( { materialSetLayout, materialSetLayout, materialSetLayout, materialSetLayout });
    cloth = std::make_shared<Cloth>( device, materialSets, glm::vec2{100} );
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
            .name("acceleration_structure_set_layout")
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
	render.graphColor.pipeline =
		builder3
			.shaderStage()
				.vertexShader(resource("graph_coloring.vert.spv"))
				.geometryShader(resource("graph_coloring.geom.spv"))
                .fragmentShader(resource("flat.frag.spv"))
            .vertexInputState().clear()
			.inputAssemblyState()
				.points()
			.rasterizationState()
				.cullNone()
                .lineWidth(2.5)
            .depthStencilState()
                .compareOpAlways()
            .layout().clear()
                .addPushConstantRange(VK_SHADER_STAGE_GEOMETRY_BIT, 0, sizeof(Camera) + sizeof(int) * 2)
                .addDescriptorSetLayout(pbdSolver->positionSetLayout)
                .addDescriptorSetLayout(pbdSolver->descriptorSetLayout)
			.name("render_graph")
			.build(render.graphColor.layout);

	auto builder4 = prototypes->cloneGraphicsPipeline();
	render.floor.pipeline =
        builder4
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

	auto builder5 = prototypes->cloneGraphicsPipeline();
	render.solid.pipeline =
        builder5
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
        builder5
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
    if(resetRequested) {
        createCloth();
        initSolvers();
        simRunning = false;
        resetRequested = false;
    }

    camera->perspective(swapChain.aspectRatio());
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
//    renderGraph(commandBuffer);

    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkCmdUpdateBuffer(commandBuffer, geometry->uboBuffer, 0, sizeof(geometry->ubo), &geometry->ubo);
    Barrier::transferWriteToComputeRead(commandBuffer, geometry->uboBuffer);

    solver->solve(commandBuffer);
    switchSolver(commandBuffer);

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
    if(geometry->type() == Geometry::Type::None) return;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.solid.pipeline.handle);
    camera->push(commandBuffer, render.solid.layout, geometry->ubo.worldSpaceXform);
    geometry->bindVertexBuffers(commandBuffer);
    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
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

void ClothDemo2::renderGraph(VkCommandBuffer commandBuffer) {
    if(!showGraph) return;

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, dummyBuffer, &offset);

    int passOffset = 0;
    for(auto pass = 0; pass < 5; ++pass) {
        const uint32_t instanceCount = pbdSolver->passSizes[pass];

        std::array<VkDescriptorSet, 2> sets{pbdSolver->positionDescriptorSet[0], pbdSolver->descriptorSet};
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.graphColor.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.graphColor.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
        camera->push(commandBuffer, render.graphColor.layout, glm::mat4{1}, VK_SHADER_STAGE_GEOMETRY_BIT);
        vkCmdPushConstants(commandBuffer, render.graphColor.layout.handle, VK_SHADER_STAGE_GEOMETRY_BIT, sizeof(Camera), sizeof(pass), &pass);
        vkCmdPushConstants(commandBuffer, render.graphColor.layout.handle, VK_SHADER_STAGE_GEOMETRY_BIT, sizeof(Camera) + sizeof(int), sizeof(passOffset), &passOffset);
        vkCmdDraw(commandBuffer, 1, instanceCount, 0, 0);

        passOffset += instanceCount;
    }
}

void ClothDemo2::renderUI(VkCommandBuffer commandBuffer) {
    auto& imGuiPlugin = plugin<ImGuiPlugin>(IM_GUI_PLUGIN);

    ImGui::Begin("Cloth Simulation");
    ImGui::SetWindowSize("Cloth Simulation", {0, 0});
    static int option = static_cast<int>(shading);

    ImGui::Text("Solver:");
    ImGui::Indent(16);
    ImGui::RadioButton("Verlet", &solverType, 0);
    ImGui::SameLine();
    ImGui::RadioButton("PBD", &solverType, 1);
    ImGui::Indent(-16);

    ImGui::Text("Cloth:");
    ImGui::Indent(16);
    static std::array<const char*, 4> matLabel{"Chenille Polyester Upholstery", "Denim", "Bengaline", "Inca Stripped"};
    ImGui::Combo("material", &materialId, matLabel.data(), matLabel.size());

    if(materialId == 0) {
        ImGui::SliderInt("color", &clothColor, 0, 2);
    }else{
        clothColor = 0;
    }
    ImGui::Indent(-16);

    ImGui::Text("Collider:");
    ImGui::Indent(16);
    static int colliderValue = static_cast<int>(collider);

    ImGui::RadioButton("Sphere", &colliderValue, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Box", &colliderValue, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Cow", &colliderValue, 2);
    ImGui::SameLine();
    ImGui::RadioButton("None", &colliderValue, 3);
    ImGui::Indent(-16);
    collider = static_cast<Collider>(colliderValue);

    auto geometryType = static_cast<Geometry::Type>(collider);
    if(geometryType != geometry->type()) {
        geometry->switchTo(static_cast<Geometry::Type>(collider));
    }

    static bool wind = solver->constants.simWind;
    ImGui::Text("Wind:");
    ImGui::Indent(16);
    if(wind){
        ImGui::SliderFloat("strength", &solver->constants.windStrength, 1, 10);
        ImGui::SliderFloat("speed", &solver->constants.windSpeed, 0.1, 1);
    }
    ImGui::Checkbox("on", &wind);
    ImGui::Indent(-16);

    solver->constants.simWind = wind;

    ImGui::Text("Debug:");
    ImGui::Indent(16);
    ImGui::RadioButton("wireframe", &option, 0);
    ImGui::SameLine();
    ImGui::RadioButton("shaded", &option, 1);
    shading = static_cast<Shading>(option);

    bool showWireframeOptions = shading == Shading::WIREFRAME;
    if(ImGui::CollapsingHeader("wireframe options", &showWireframeOptions, ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::Checkbox("points", &showPoints);
        ImGui::Checkbox("normals", &showNormals);
    }
    ImGui::Indent(-16);

//    ImGui::SliderFloat("shine", &shine, 1, 100);
//    ImGui::Text("%d iteration(s), timeStep: %.3f ms", numIterations, frameTime * 1000);
//    ImGui::Text("Application average %.3f ms/frame, (%d FPS)", 1000.0/framePerSecond, framePerSecond);
//    ImGui::Text("compute: %.3f ms", computeDuration);
//    ImGui::Text(fmt::format("Camera position: {}, target: {}", cameraController->position(), cameraController->target).c_str());
    if(ImGui::Button("Start") && !simRunning){
        simRunning = true;
    }
    ImGui::SameLine();
    if(ImGui::Button("Reset") && simRunning && !resetRequested) {
        resetRequested = true;
        invalidateSwapChain();
    }
    ImGui::End();

    if(collider != Collider::None) {

        ImGui::Begin("Collider transform");
        ImGui::Text("Position:");
        auto& pos = xform.position;
        ImGui::Indent(16);
        ImGui::SliderFloat("x##pos", &pos.x, -3, 3);
        ImGui::SliderFloat("y##pos", &pos.y, -3, 3);
        ImGui::SliderFloat("z##pos", &pos.z, -3, 3);
        ImGui::Indent(-16);

        ImGui::Text("Orientation:");
        auto& orient = xform.rotation;
        ImGui::Indent(16);
        ImGui::SliderFloat("x##orient", &orient.x, 0, 360);
        ImGui::SliderFloat("y##orient", &orient.y, 0, 360);
        ImGui::SliderFloat("z##orient", &orient.z, 0, 360);
        ImGui::Indent(-16);

        ImGui::Text("Scale:");
        auto& scale = xform.scale;
        ImGui::Indent(16);
        ImGui::SliderFloat("x##scale", &scale.x, 0.5, 2);
        ImGui::SliderFloat("y##scale", &scale.y, 0.5, 2);
        ImGui::SliderFloat("z##scale", &scale.z, 0.5, 2);
        ImGui::Indent(-16);

        ImGui::End();
    }

    imGuiPlugin.draw(commandBuffer);
}

void ClothDemo2::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }
    auto cam = camera->cam();
    glfwSetWindowTitle(window, fmt::format("{}, vertex count {}, fps {}", title, cloth->vertexCount(), framePerSecond).c_str());

    auto& gTransform = geometry->ubo.worldSpaceXform;
    gTransform = glm::translate(glm::mat4(1), xform.position);
    gTransform = glm::scale(gTransform, xform.scale);
    gTransform = glm::rotate(gTransform, glm::radians(xform.rotation.z), {0, 0, 1});
    gTransform = glm::rotate(gTransform, glm::radians(xform.rotation.y), {0, 1, 0});
    gTransform = glm::rotate(gTransform, glm::radians(xform.rotation.z), {1, 0, 0});
    geometry->ubo.localSpaceXform = glm::inverse(gTransform);

    if(simRunning) {
        solver->constants.collider = static_cast<int>(collider);
        solver->update(time);
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
    geometry->initialize(device);
    collider = static_cast<Collider>(geometry->type());
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

    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    auto atomicFeatures = findExtension<VkPhysicalDeviceShaderAtomicFloatFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT, deviceCreateNextChain);
    atomicFeatures->shaderBufferFloat32AtomicAdd = VK_TRUE;
    atomicFeatures->shaderBufferFloat32Atomics = VK_TRUE;
}

void ClothDemo2::switchSolver(VkCommandBuffer commandBuffer) {
    static int prevSolverType = solverType;

    if(!simRunning || prevSolverType == solverType) return;

    auto source = positions[prevSolverType][0];
    auto destination = positions[solverType][0];

    VkBufferCopy region{0, 0, source.size};
    vkCmdCopyBuffer(commandBuffer, source, destination, 1, &region);

    source = positions[prevSolverType][1];
    destination = positions[solverType][1];

    region = VkBufferCopy{0, 0, source.size};
    vkCmdCopyBuffer(commandBuffer, source, destination, 1, &region);

    Barrier::transferWriteToComputeWrite(commandBuffer);

    solver = solverType == 0 ? verletSolver.get() : pbdSolver.get();
    prevSolverType = solverType;
}

int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = settings.height = 1024;
        settings.depthTest = true;
        settings.enableBindlessDescriptors = false;
        settings.enabledFeatures.fillModeNonSolid = true;
        settings.enabledFeatures.geometryShader = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();

        auto app = ClothDemo2{settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}