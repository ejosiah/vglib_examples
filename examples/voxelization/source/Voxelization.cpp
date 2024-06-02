#include "Voxelization.hpp"
#include "AppContext.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "Barrier.hpp"

Voxelization::Voxelization(const Settings& settings) : VulkanBaseApp("Computatinal Voxelization", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../../examples/voxelization");
    fileManager().addSearchPathFront("../../examples/voxelization/data");
    fileManager().addSearchPathFront("../../examples/voxelization/spv");
    fileManager().addSearchPathFront("../../examples/voxelization/models");
    fileManager().addSearchPathFront("../../examples/voxelization/textures");
}

void Voxelization::initApp() {
    createDescriptorPool();
    AppContext::init(device, descriptorPool);
    initFloor();
    initCube();
    loadModel();
    initVoxelData();
    initCamera();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createVoxelStorage();
    createPipelineCache();
    createRenderPipeline();
    createComputePipelines();
}

void Voxelization::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.velocity = glm::vec3{5};
    cameraSettings.acceleration = glm::vec3(5);
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);
    cameraSettings.horizontalFov = true;
    camera = std::make_unique<SpectatorCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera->lookAt({-5, 2, 3}, {0, 0, 0}, {0, 1, 0});
}


void Voxelization::createDescriptorPool() {
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

void Voxelization::createDescriptorSetLayouts() {
    voxels.descriptorSetLayout = 
        device.descriptorSetLayoutBuilder()
            .name("voxel_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();
    
    voxels.descriptorSet = descriptorPool.allocate( { voxels.descriptorSetLayout }).front();
}

void Voxelization::updateDescriptorSets(){
}

void Voxelization::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void Voxelization::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void Voxelization::createRenderPipeline() {
    //    @formatter:off
    auto builder = prototypes->cloneGraphicsPipeline();
    pipelines.triangle.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("voxelizeTriangleParallel.vert.spv"))
                    .geometryShader(resource("voxelizeTriangleParallel.geom.spv"))
                .rasterizationState()
                    .enableRasterizerDiscard()
                .dynamicState()
                    .primitiveTopology()
                .layout().clear()
                    .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4))
                    .addDescriptorSetLayout(voxels.descriptorSetLayout)
                .name("triangle_parallel_voxelization")
                .build(pipelines.triangle.layout);

    pipelines.fragment.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("voxelizeFragmentParallel.vert.spv"))
                    .geometryShader(resource("voxelizeFragmentParallel.geom.spv"))
                    .fragmentShader(resource("voxelizeFragmentParallel.frag.spv"))
                .dynamicState()
                    .primitiveTopology()
                .layout().clear()
                    .addPushConstantRange(VK_SHADER_STAGE_ALL, 0, sizeof(glm::mat4) * 2)
                    .addDescriptorSetLayout(voxels.descriptorSetLayout)
                .name("fragment_parallel_voxelization")
                .build(pipelines.fragment.layout);

	auto builder1 = prototypes->cloneGraphicsPipeline();
	pipelines.render.pipeline =
        builder1
			.shaderStage()
				.vertexShader(resource("solid.vert.spv"))
                .fragmentShader(resource("solid.frag.spv"))
            .inputAssemblyState()
                .triangleStrip()
            .dynamicState()
                .primitiveTopology()
                .cullMode()
            .layout()
                .addDescriptorSetLayout(AppContext::instanceSetLayout())
			.name("solid_render")
			.build(pipelines.render.layout);

    auto builder2 = prototypes->cloneGraphicsPipeline();
    pipelines.rayMarch.pipeline =
        builder2
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
                .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Camera) + sizeof(glm::vec4) * 2)
                .addDescriptorSetLayout(voxels.descriptorSetLayout)
			.name("ray_march")
			.build(pipelines.rayMarch.layout);
    //    @formatter:on

}

void Voxelization::createComputePipelines() {
    auto module = device.createShaderModule(resource("gen_voxel_transforms.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    pipelines.genVoxelTransforms.layout = device.createPipelineLayout( {  voxels.descriptorSetLayout } );

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = pipelines.genVoxelTransforms.layout.handle;

    pipelines.genVoxelTransforms.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("generate_voxel_xforms", pipelines.genVoxelTransforms.pipeline.handle);
}


void Voxelization::onSwapChainDispose() {
    dispose(pipelines.triangle.pipeline);
    dispose(pipelines.render.pipeline);
    dispose(pipelines.rayMarch.pipeline);
}

void Voxelization::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
    initFloor();
}

VkCommandBuffer *Voxelization::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
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

    floor.render(commandBuffer, *camera);

    if(renderType == RenderType::Default){
        renderModel(commandBuffer);
    }else if(renderType == RenderType::Voxels) {
        renderVoxels(commandBuffer);
    }else if(renderType == RenderType::RayMarch) {
        rayMarch(commandBuffer);
    }

//    if(refreshVoxel) {
        voxelize(commandBuffer);
//    }
    renderUI(commandBuffer);
    vkCmdEndRenderPass(commandBuffer);

//    if(refreshVoxel) {
        generateVoxelTransforms(commandBuffer);
        refreshVoxel = false;
//    }

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void Voxelization::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }
    auto cam = camera->cam();
    glfwSetWindowTitle(window, fmt::format("{}, pos: {}", title, camera->position()).c_str());
}

void Voxelization::checkAppInputs() {
    camera->processInput();
}

void Voxelization::cleanup() {
    AppContext::shutdown();
}

void Voxelization::onPause() {
    VulkanBaseApp::onPause();
}

void Voxelization::initFloor() {
    floor = Floor{ device, *prototypes };
    floor.init();
}

void Voxelization::loadModel() {
    std::vector<mesh::Mesh> meshes;
    mesh::load(meshes, resource("cow.ply"));
    auto& mesh = meshes.front();
    bounds.min = glm::vec4(mesh.bounds.min, 0);
    bounds.max = glm::vec4(mesh.bounds.max, 0);
    spdlog::info("bounds [min : {}, max: {} ]", mesh.bounds.min, mesh.bounds.max);

    auto dim = mesh.bounds.max - mesh.bounds.min;
    auto invMaxAxis = 1.f/glm::max(dim.x, glm::max(dim.y, dim.x));
    voxels.transform = glm::scale(glm::mat4{1}, glm::vec3(invMaxAxis));
    voxels.transform = glm::translate(voxels.transform, -mesh.bounds.min);

    auto tmin = voxels.transform * glm::vec4(mesh.bounds.min, 1);
    auto tmax = voxels.transform * glm::vec4(mesh.bounds.max, 1);
    spdlog::info("bounds [min : {}, max: {} ]", tmin.xyz(), tmax.xyz());

    model.vertices = device.createDeviceLocalBuffer(mesh.vertices.data(), BYTE_SIZE(mesh.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    model.indices = device.createDeviceLocalBuffer(mesh.indices.data(), BYTE_SIZE(mesh.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void Voxelization::initCube() {
    auto c = primitives::cube();
    cube.vertices = device.createDeviceLocalBuffer(c.vertices.data(), BYTE_SIZE(c.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    cube.indices = device.createDeviceLocalBuffer(c.indices.data(), BYTE_SIZE(c.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void Voxelization::initVoxelData() {
    VoxelData data{};
    voxels.dataBuffer = device.createCpuVisibleBuffer(&data, sizeof(data), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    voxels.data = reinterpret_cast<VoxelData*>(voxels.dataBuffer.map());
    voxels.transforms = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(glm::mat4) * 500000, "voxel_transforms_buffer");
}

void Voxelization::renderModel(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.render.pipeline.handle);
    AppContext::bindInstanceDescriptorSets(commandBuffer, pipelines.render.layout);
    camera->push(commandBuffer, pipelines.render.layout);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, model.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, model.indices, 0, VK_INDEX_TYPE_UINT32);

    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_BACK_BIT);
    vkCmdDrawIndexed(commandBuffer, model.indices.sizeAs<uint32_t>(), 1, 0, 0, 0);
}

void Voxelization::renderVoxels(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.render.layout.handle, 0, 1, &voxels.transformsDescriptorSet, 0, 0);

    camera->push(commandBuffer, pipelines.render.layout);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, cube.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, cube.indices, 0, VK_INDEX_TYPE_UINT32);

    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_BACK_BIT);
    vkCmdDrawIndexed(commandBuffer, cube.indices.sizeAs<uint32_t>(), voxels.data->numVoxels, 0, 0, 0);
}

void Voxelization::rayMarch(VkCommandBuffer commandBuffer) {
    static uint32_t pcSize = sizeof(Camera) + sizeof(bounds);
    static std::vector<char> pc(pcSize);
    std::memcpy(pc.data(), &camera->camera, sizeof(Camera));
    std::memcpy(pc.data() + sizeof(Camera), &bounds.min.x, sizeof(bounds));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.rayMarch.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.rayMarch.layout.handle, 0, 1, &voxels.descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, pipelines.rayMarch.layout.handle, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, pc.size(), pc.data());
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void Voxelization::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Voxelization");
    ImGui::SetWindowSize({0, 0});
    ImGui::Text("Method:");
    ImGui::Indent(16);

    static int methodOpt = static_cast<int>(method);
    ImGui::RadioButton("Triangle", &methodOpt, static_cast<int>(Method::TriangleParallel));
    ImGui::SameLine();
    ImGui::RadioButton("Fragment", &methodOpt, static_cast<int>(Method::FragmentParallel));
    ImGui::SameLine();
    ImGui::RadioButton("Hybrid", &methodOpt, static_cast<int>(Method::Hybrid));
    ImGui::Indent(-16);

    ImGui::Text("Voxel Size:");
    ImGui::Indent(16);
    static int vSize = voxels.size;
    ImGui::RadioButton("128", &vSize, 128);
    ImGui::SameLine();
    ImGui::RadioButton("256", &vSize, 256);
    ImGui::SameLine();
    ImGui::RadioButton("512", &vSize, 512);
    ImGui::Indent(-16);

    ImGui::Text("Render:");
    ImGui::Indent(16);
    int renderOpt = static_cast<int>(renderType);
    ImGui::RadioButton("Default", &renderOpt, static_cast<int>(RenderType::Default));
    ImGui::SameLine();
    ImGui::RadioButton("Voxels", &renderOpt, static_cast<int>(RenderType::Voxels));
    ImGui::SameLine();
    ImGui::RadioButton("RayMatch", &renderOpt, static_cast<int>(RenderType::RayMarch));
    renderType = static_cast<RenderType>(renderOpt);
    ImGui::Indent(-16);

    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);

    refreshVoxel |= vSize != voxels.size;
    voxels.size = vSize;
    method = static_cast<Method>(methodOpt);
}

void Voxelization::voxelize(VkCommandBuffer commandBuffer) {
    fragmentParallelVoxelization(commandBuffer);
}

void Voxelization::triangleParallelVoxelization(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.triangle.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.triangle.layout.handle, 0, 1, &voxels.descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, pipelines.triangle.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), glm::value_ptr(voxels.transform));
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, model.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, model.indices, 0, VK_INDEX_TYPE_UINT32);

    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdDrawIndexed(commandBuffer, model.indices.sizeAs<uint32_t>(), 1, 0, 0, 0);
}

void Voxelization::fragmentParallelVoxelization(VkCommandBuffer commandBuffer) {
    static std::array<glm::mat4, 2> pushConstants{};
    VkDeviceSize offset = 0;
    pushConstants[0] = voxels.transform;
    pushConstants[1] = fpMatrix(glm::ivec3(voxels.size));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.fragment.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.fragment.layout.handle, 0, 1, &voxels.descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, pipelines.fragment.layout.handle, VK_SHADER_STAGE_ALL, 0, BYTE_SIZE(pushConstants), pushConstants.data());
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, model.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, model.indices, 0, VK_INDEX_TYPE_UINT32);

    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdDrawIndexed(commandBuffer, model.indices.sizeAs<uint32_t>(), 1, 0, 0, 0);
}

void Voxelization::generateVoxelTransforms(VkCommandBuffer commandBuffer) {
    glm::uvec3 wg{ glm::max(1u, voxels.size/8)};


    VkImageMemoryBarrier voxelBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    voxelBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    voxelBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    voxelBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    voxelBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    voxelBarrier.image = voxels.texture.image;
    voxelBarrier.subresourceRange = DEFAULT_SUB_RANGE;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, 0, 0, 0, 1, &voxelBarrier);

    VoxelData data{};
    data.worldToVoxelTransform = voxels.transform;
    data.voxelToWordTransform = glm::inverse(voxels.transform);
    vkCmdUpdateBuffer(commandBuffer, voxels.dataBuffer, 0, sizeof(VoxelData), &data);
    Barrier::transferWriteToComputeRead(commandBuffer, voxels.dataBuffer);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.genVoxelTransforms.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.genVoxelTransforms.layout.handle, 0, 1, &voxels.descriptorSet, 0, 0);
    vkCmdDispatch(commandBuffer, wg.x, wg.y, wg.z);
    Barrier::computeWriteToFragmentRead(commandBuffer, { voxels.transforms });
}

void Voxelization::createVoxelStorage() {

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    voxels.texture.sampler = device.createSampler(samplerInfo);
    textures::create(device, voxels.texture, VK_IMAGE_TYPE_3D, VK_FORMAT_R32_UINT, Dimension3D<uint32_t>(voxels.size));
    voxels.texture.image.transitionLayout(commandPool, VK_IMAGE_LAYOUT_GENERAL);
    updateVoxelDescriptorSet();
}

void Voxelization::updateVoxelDescriptorSet() {
    auto writes = initializers::writeDescriptorSets<4>();

    writes[0].dstSet = voxels.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo imageInfo{VK_NULL_HANDLE, voxels.texture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[0].pImageInfo = &imageInfo;
    
    writes[1].dstSet = voxels.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo xformInfo{ voxels.transforms, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &xformInfo;

    writes[2].dstSet = voxels.descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo dataInfo{ voxels.dataBuffer, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &dataInfo;

    writes[3].dstSet = voxels.descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo texInfo{voxels.texture.sampler.handle, voxels.texture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[3].pImageInfo = &texInfo;

    voxels.transformsDescriptorSet = AppContext::allocateInstanceDescriptorSet();

    VkCopyDescriptorSet copySet{ VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET };
    copySet.srcSet = voxels.descriptorSet;
    copySet.srcBinding = 1;
    copySet.srcArrayElement = 0;
    copySet.dstSet = voxels.transformsDescriptorSet;
    copySet.dstBinding = 0;
    copySet.dstArrayElement = 0;
    copySet.descriptorCount = 1;
    
    device.updateDescriptorSets(writes, { copySet });
}

void Voxelization::endFrame() {
    if(refreshVoxel) {
        createVoxelStorage();
    }
}

glm::mat4 Voxelization::fpMatrix(glm::ivec3 voxelDim) {
    float l = 0.0f; //left
    float b = 0.0f; //bottom
    float n = 0.0f; //near

    auto r = (float)voxelDim.x; //right
    auto t = (float)voxelDim.y; //top
    auto f = (float)voxelDim.z; //far

    float inv_dx = 1.0f / (r-l);
    float inv_dy = 1.0f / (t-b);
    float inv_dz = 1.0f / (f-n);

    glm::mat4 matrix{
            2*inv_dx,        0,               0,             0,
            0,               2*inv_dy,        0,             0,
            0,               0,               inv_dz,        0,
            -1*(r+l)*inv_dx, -1*(t+b)*inv_dy, 0,             1
    };

    return matrix;
}

int main(){
    try{

        Settings settings;
        settings.width = 1024;
        settings.width = 1024;
        settings.depthTest = true;
        settings.enableBindlessDescriptors = false;
        settings.enabledFeatures.geometryShader = VK_TRUE;
        settings.enabledFeatures.vertexPipelineStoresAndAtomics = VK_TRUE;
        settings.enabledFeatures.fragmentStoresAndAtomics = VK_TRUE;
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();

        auto app = Voxelization{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}