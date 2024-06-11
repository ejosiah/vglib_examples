#include "MarchingCubes2.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include <openvdb/openvdb.h>
#include <openvdb/io/Stream.h>
#include "Barrier.hpp"

MarchingCubes2::MarchingCubes2(const Settings& settings) : VulkanBaseApp("Marching Cubes", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../../examples/marching_cubes2");
    fileManager().addSearchPathFront("../../examples/marching_cubes2/data");
    fileManager().addSearchPathFront("../../examples/marching_cubes2/spv");
    fileManager().addSearchPathFront("../../examples/marching_cubes2/models");
    fileManager().addSearchPathFront("../../examples/marching_cubes2/textures");
}

void MarchingCubes2::initApp() {
    openvdb::initialize();
    checkMeshShaderSupport();
    createDescriptorPool();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    loadVoxel();
    initCamera();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    initMarcher();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
}

void MarchingCubes2::checkMeshShaderSupport() {
    VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
    features.pNext = &meshFeatures;

    vkGetPhysicalDeviceFeatures2(device.physicalDevice, &features);
    meshShaderSupported = meshFeatures.meshShader && meshFeatures.taskShader;
}

void MarchingCubes2::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.velocity = glm::vec3{5};
    cameraSettings.acceleration = glm::vec3(5);
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);
    cameraSettings.horizontalFov = true;
    camera = std::make_unique<SpectatorCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera->lookAt({-5, 2, 3}, {0, 0, 0}, {0, 1, 0});
}

void MarchingCubes2::loadVoxel() {
    openvdb::io::File file(resource("cow.vdb"));

    file.open();

    auto grid = openvdb::gridPtrCast<openvdb::FloatGrid>(file.readGrid(file.beginName().gridName()));
    openvdb::Vec3i boxMax = grid->getMetadata<openvdb::Vec3IMetadata>("file_bbox_max")->value();
    int64_t numVoxels = grid->activeVoxelCount();
    auto bounds = grid->evalActiveVoxelBoundingBox();
    auto dim = grid->evalActiveVoxelDim();

    spdlog::info("num active voxels: {}", numVoxels);
    spdlog::info("dim: [{}, {}, {}]", dim.x(), dim.y(), dim.z());
    spdlog::info("bounds: [{}, {}, {}], [{}, {}, {}]"
                 , bounds.min().x(), bounds.min().y(), bounds.min().z()
                 , bounds.max().x(), bounds.max().y(), bounds.max().z());


    voxels.dim = { dim.x(), dim.y(), dim.z() };

    openvdb::Vec3d bMin = grid->indexToWorld(bounds.min());
    openvdb::Vec3d bMax = grid->indexToWorld(bounds.max());
    voxels.bounds.min = { bMin.x(), bMin.y(), bMin.z() };
    voxels.bounds.max = { bMax.x(), bMax.y(), bMax.z() };
    voxels.voxelSize = grid->voxelSize().x();

    std::vector<float> data(dim.x() * dim.y() * dim.z(), grid->background());

    auto iBoxMin = grid->getMetadata<openvdb::Vec3IMetadata>("file_bbox_min")->value();

    auto value = grid->beginValueOn();
   do {
       auto coord = value.getCoord();
       auto x = coord.x() + std::abs(iBoxMin.x());
       auto y = coord.y() + std::abs(iBoxMin.y());
       auto z = coord.z() + std::abs(iBoxMin.z());

       auto index = (z * dim.y() + y) * dim.x() + x;
       data[index] = *value;
//      spdlog::info("index : {} coords: [{}, {}, {}] value: {}", index, x, y, z, *value);
   }while(value.next());


    textures::create(device, voxels.texture, VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT, data.data(), voxels.dim);

    file.close();

//    auto offset = -voxels.bounds.min.y;
//    voxels.bounds.min.y += offset;
//    voxels.bounds.max.y += offset;
    auto fdim = voxels.bounds.max - voxels.bounds.min;
    auto invMaxAxis = 1.f/fdim;
    voxels.transform = glm::scale(glm::mat4{1}, invMaxAxis);
    voxels.transform = glm::translate(voxels.transform, -voxels.bounds.min);

    auto tmin = voxels.transform * glm::vec4(voxels.bounds.min, 1);
    auto tmax = voxels.transform * glm::vec4(voxels.bounds.max, 1);
    spdlog::info("bounds [min : {}, max: {} ]", tmin.xyz(), tmax.xyz());

    VoxelData voxelData{voxels.transform, glm::inverse(voxels.transform), static_cast<int>(numVoxels)};
    voxels.dataBuffer = device.createCpuVisibleBuffer(&voxelData, sizeof(voxelData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    voxels.data = reinterpret_cast<VoxelData*>(voxels.dataBuffer.map());

    pipelines.cubeMarcher.constants.bmin = voxels.bounds.min;
    pipelines.cubeMarcher.constants.bmax = voxels.bounds.max;

}

void MarchingCubes2::initMarcher() {
    cubeMarcher = Marcher{ &voxels, voxels.voxelSize/2};
    cubeMarcher.init();
    result = cubeMarcher.generateMesh(voxels.voxelSize * cubeSizeMultiplier);
}

void MarchingCubes2::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 3> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void MarchingCubes2::createDescriptorSetLayouts() {
    voxels.descriptorSetLayout = 
        device.descriptorSetLayoutBuilder()
            .name("voxel_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_TASK_BIT_EXT)
            .createLayout();
    
    voxels.descriptorSet = descriptorPool.allocate( { voxels.descriptorSetLayout }).front();	
}

void MarchingCubes2::updateDescriptorSets(){
    auto writes = initializers::writeDescriptorSets<2>();

    writes[0].dstSet = voxels.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo texInfo{voxels.texture.sampler.handle, voxels.texture.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[0].pImageInfo = &texInfo;


    writes[1].dstSet = voxels.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo dataInfo{ voxels.dataBuffer, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &dataInfo;


    device.updateDescriptorSets(writes);
}

void MarchingCubes2::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void MarchingCubes2::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void MarchingCubes2::createRenderPipeline() {
    //    @formatter:off
    pipelines.rayMarch.pipeline =
        prototypes->cloneGraphicsPipeline()
			.shaderStage()
				.vertexShader(resource("ray_march.vert.spv"))
                .fragmentShader(resource("ray_march.frag.spv"))
            .vertexInputState().clear()
                .addVertexBindingDescriptions(ClipSpace::bindingDescription())
                .addVertexAttributeDescriptions(ClipSpace::attributeDescriptions())
            .inputAssemblyState()
                .triangleStrip()
			.rasterizationState()
				.cullNone()
            .depthStencilState()
                .compareOpLess()
            .layout().clear()
                .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Camera) + sizeof(voxels.bounds))
                .addDescriptorSetLayout(voxels.descriptorSetLayout)
			.name("ray_march")
			.build(pipelines.rayMarch.layout);

	pipelines.render.pipeline =
        prototypes->cloneGraphicsPipeline()
			.shaderStage()
				.vertexShader(resource("solid.vert.spv"))
                .fragmentShader(resource("solid.frag.spv"))
            .inputAssemblyState()
                .triangles()
            .layout()
                .addDescriptorSetLayout(AppContext::instanceSetLayout())
			.name("solid_render")
			.build(pipelines.render.layout);

	pipelines.cubeMarcher.pipeline =
        prototypes->cloneGraphicsPipeline()
			.shaderStage()
                .taskSShader(resource("marching_cubes.task.spv"))
				.meshShader(resource("marching_cube.mesh.spv"))
                .fragmentShader(resource("solid.frag.spv"))
            .inputAssemblyState()
                .triangles()
            .dynamicState()
                .polygonModeEnable()
            .layout().clear()
                .addPushConstantRange(VK_SHADER_STAGE_MESH_BIT_EXT, 0, sizeof(pipelines.cubeMarcher.constants))
                .addDescriptorSetLayout(voxels.descriptorSetLayout)
                .addDescriptorSetLayout(cubeMarcher.lutDescriptorSetLayout())
                .addDescriptorSetLayout(cubeMarcher.vertexDescriptorSetLayout())
			.name("mesh_shader_cube_marcher")
			.build(pipelines.cubeMarcher.layout);
    //    @formatter:on
}


void MarchingCubes2::onSwapChainDispose() {
    dispose(pipelines.rayMarch.pipeline);
}

void MarchingCubes2::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    camera->perspective(swapChain.aspectRatio());
}

VkCommandBuffer *MarchingCubes2::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    static std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = {0, 0, 1, 1};
    clearValues[1].depthStencil = {1.0, 0u};

    VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
    rPassInfo.clearValueCount = COUNT(clearValues);
    rPassInfo.pClearValues = clearValues.data();
    rPassInfo.framebuffer = framebuffers[imageIndex];
    rPassInfo.renderArea.offset = {0u, 0u};
    rPassInfo.renderArea.extent = swapChain.extent;
    rPassInfo.renderPass = renderPass;

    vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    AppContext::renderFloor(commandBuffer, *camera);

//    rayMarch(commandBuffer);

    if(useMeshShader){
        static std::array<VkDescriptorSet, 3> sets{};
        sets[0] = voxels.descriptorSet;
        sets[1] = cubeMarcher.lutDescriptorSet();
        sets[2] = cubeMarcher.vertexDescriptorSet();

        auto cubeSize = voxels.voxelSize * cubeSizeMultiplier;
        pipelines.cubeMarcher.constants.cubeSize = cubeSize;
        glm::uvec3 gc{ ((voxels.bounds.max - voxels.bounds.min) + cubeSize)/cubeSize};
        gc /= glm::uvec3(4, 4, 2);


        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.cubeMarcher.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.cubeMarcher.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdPushConstants(commandBuffer, pipelines.cubeMarcher.layout.handle, VK_SHADER_STAGE_MESH_BIT_EXT, 0, sizeof(pipelines.cubeMarcher.constants), &pipelines.cubeMarcher.constants);

        if(shadingMode == 0) {
            vkCmdSetPolygonModeEXT(commandBuffer, VK_POLYGON_MODE_LINE);
        }else {
            vkCmdSetPolygonModeEXT(commandBuffer, VK_POLYGON_MODE_FILL);
        }

        vkCmdDrawMeshTasksEXT(commandBuffer, gc.x, gc.y, gc.z);


    }else {
        if (shadingMode == 0) {
            renderMeshWireframe(commandBuffer);
        } else {
            renderMesh(commandBuffer);
        }
    }
    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    if(!useMeshShader) {
        cubeMarcher.generateMesh(voxels.voxelSize * cubeSizeMultiplier);
        Barrier::computeWriteToVertexDraw(commandBuffer, { result.vertices, result.indices, result.drawCmd });
    }

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void MarchingCubes2::renderMesh(VkCommandBuffer commandBuffer) {
    AppContext::renderSolid(commandBuffer, *camera, [&]{
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, result.vertices, &offset);
        vkCmdBindIndexBuffer(commandBuffer, result.indices, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexedIndirect(commandBuffer, result.drawCmd, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
    });
}

void MarchingCubes2::renderMeshWireframe(VkCommandBuffer commandBuffer) {
    AppContext::renderWireframe(commandBuffer, *camera, [&]{
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, result.vertices, &offset);
        vkCmdBindIndexBuffer(commandBuffer, result.indices, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexedIndirect(commandBuffer, result.drawCmd, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
    }, {1, 1, 0});
}

void MarchingCubes2::rayMarch(VkCommandBuffer commandBuffer) {
    static uint32_t pcSize = sizeof(Camera) + sizeof(voxels.bounds);
    static std::vector<char> pc(pcSize);
    std::memcpy(pc.data(), &camera->camera, sizeof(Camera));
    std::memcpy(pc.data() + sizeof(Camera), &voxels.bounds.min.x, sizeof(voxels.bounds));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.rayMarch.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.rayMarch.layout.handle, 0, 1, &voxels.descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, pipelines.rayMarch.layout.handle, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, pc.size(), pc.data());
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void MarchingCubes2::renderUI(VkCommandBuffer commandBuffer) {

    ImGui::Begin("Marching cubes");
    ImGui::SetWindowSize({300, 200});

    if(ImGui::CollapsingHeader("Shading", ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::RadioButton("wireframe", &shadingMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Solid", &shadingMode, 1);
    }

    if(ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::SliderFloat("Cube size", &cubeSizeMultiplier, 0.25, 4.0);

        ImGui::Checkbox("Use mesh shader", &useMeshShader);

        if(!useMeshShader) {
            bool mergeDuplicateVertices = true;
            ImGui::Checkbox("Merge duplicate vertices", &mergeDuplicateVertices);
        }

        ImGui::Text("%d vertices generated", *result.numVertices);
    }

    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void MarchingCubes2::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }
    pipelines.cubeMarcher.constants.camera = camera->cam();
    glfwSetWindowTitle(window, fmt::format("{}, FPS {}", title, framePerSecond).c_str());
}

void MarchingCubes2::checkAppInputs() {
    camera->processInput();
}

void MarchingCubes2::cleanup() {
    AppContext::shutdown();
}

void MarchingCubes2::onPause() {
    VulkanBaseApp::onPause();
}

void MarchingCubes2::endFrame() {
//    if(!ImGui::IsAnyItemActive() && !useMeshShader){
//        static float prevCubeSizeMultiplier = cubeSizeMultiplier;
//        if(prevCubeSizeMultiplier != cubeSizeMultiplier) {
//            cubeMarcher.generateMesh(voxels.voxelSize * cubeSizeMultiplier);
//            prevCubeSizeMultiplier = cubeSizeMultiplier;
//        }
//    }
}

void MarchingCubes2::beforeDeviceCreation() {
    static VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };
    meshFeatures.meshShader = VK_TRUE;
    meshFeatures.taskShader = VK_TRUE;
    meshFeatures.multiviewMeshShader = VK_TRUE;
    meshFeatures.meshShaderQueries = VK_TRUE;

    static VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dsFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };
    dsFeatures.extendedDynamicState3PolygonMode = VK_TRUE;
    meshFeatures.pNext = &dsFeatures;

    deviceCreateNextChain = &meshFeatures;

}


int main(){
    try{

        Settings settings;
        settings.depthTest = true;
        settings.enableBindlessDescriptors = false;
        settings.enabledFeatures.fillModeNonSolid = true;
        settings.deviceExtensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();

        auto app = MarchingCubes2{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}