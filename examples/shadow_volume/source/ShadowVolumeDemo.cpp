#include "ShadowVolumeDemo.hpp"
#include <meshoptimizer.h>
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "primitives.h"
#include "Mesh.h"
#include "Phong.h"

ShadowVolumeDemo::ShadowVolumeDemo(const Settings &settings) : VulkanBaseApp("graphics sandbox", settings) {
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/shadow_volume");
    fileManager.addSearchPathFront("../../examples/shadow_volume/data");
    fileManager.addSearchPathFront("../../examples/shadow_volume/spv");
    fileManager.addSearchPathFront("../../examples/shadow_volume/models");
    fileManager.addSearchPathFront("../../examples/shadow_volume/textures");

    colorWriteEnabledFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COLOR_WRITE_ENABLE_FEATURES_EXT;
    colorWriteEnabledFeature.pNext = VK_NULL_HANDLE;
    colorWriteEnabledFeature.colorWriteEnable = VK_TRUE;
    deviceCreateNextChain = &colorWriteEnabledFeature;
}

void ShadowVolumeDemo::initApp() {
    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynamicSF{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
    features2.pNext = &extDynamicSF;
    vkGetPhysicalDeviceFeatures2(device, & features2);
    createDescriptorPool();
    initCamera();
    initBuffers();
    initUBO();
    createDescriptorSet();
    updateDescriptorSet();
    createCommandPool();
    createPipeline();

    xform = glm::translate(xform, {0, 1.5, 0});
    xform = glm::scale(xform, {1, 3.0, 1});

    xform1 = glm::translate(xform1, {0, 1, 2});
    xform1 = glm::scale(xform1, {1, 2.0, 1});
}

void ShadowVolumeDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}


VkCommandBuffer *ShadowVolumeDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
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

    if(!showSilhouette) {
        renderSceneIntoDepthBuffer(commandBuffer);
        renderSceneShadowVolumeIntoStencilBuffer(commandBuffer);
        renderScene(commandBuffer);
        if(showShadowVolume) {
            visualizeShadowVolume(commandBuffer);
        }
    }else{
        renderSceneIntoDepthBuffer(commandBuffer);
        renderScene(commandBuffer);
        renderSilhouette(commandBuffer);
    }
    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void ShadowVolumeDemo::renderSceneIntoDepthBuffer(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, depthOnly.pipeline);
    camera->push(commandBuffer, depthOnly.layout, xform, VK_SHADER_STAGE_VERTEX_BIT);
    cube.draw(commandBuffer);

    camera->push(commandBuffer, shadow_volume.layout, xform1,  VK_SHADER_STAGE_GEOMETRY_BIT);
    cube.draw(commandBuffer);

    camera->push(commandBuffer, depthOnly.layout, glm::rotate(glm::mat4(1), -glm::half_pi<float>(), {1, 0, 0}), VK_SHADER_STAGE_VERTEX_BIT);
    plane.draw(commandBuffer);

}

void ShadowVolumeDemo::renderSceneShadowVolumeIntoStencilBuffer(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_volume.pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_volume.layout, 0, 1, &descriptorSet, 0, VK_NULL_HANDLE);
//    camera->push(commandBuffer, shadow_volume.layout, xform,  VK_SHADER_STAGE_GEOMETRY_BIT);
//    cube.draw(commandBuffer);

    camera->push(commandBuffer, shadow_volume.layout, xform1,  VK_SHADER_STAGE_GEOMETRY_BIT);
    cube.draw(commandBuffer);

    camera->push(commandBuffer, shadow_volume.layout, glm::rotate(glm::mat4(1), -glm::half_pi<float>(), {1, 0, 0}), VK_SHADER_STAGE_GEOMETRY_BIT);
    plane.draw(commandBuffer);
}

void ShadowVolumeDemo::renderSilhouette(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, silhouette.pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, silhouette.layout, 0, 1, &descriptorSet, 0, VK_NULL_HANDLE);
    camera->push(commandBuffer, silhouette.layout, xform,  VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT);
    cube.draw(commandBuffer);

    camera->push(commandBuffer, shadow_volume.layout, xform1,  VK_SHADER_STAGE_GEOMETRY_BIT);
    cube.draw(commandBuffer);

    camera->push(commandBuffer, silhouette.layout, glm::rotate(glm::mat4(1), -glm::half_pi<float>(), {1, 0, 0}), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT);
    plane.draw(commandBuffer);
}

void ShadowVolumeDemo::visualizeShadowVolume(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_volume_visual.pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_volume_visual.layout, 0, 1, &descriptorSet, 0, VK_NULL_HANDLE);
    camera->push(commandBuffer, shadow_volume_visual.layout, xform,  VK_SHADER_STAGE_GEOMETRY_BIT);
    cube.draw(commandBuffer);

    camera->push(commandBuffer, shadow_volume.layout, xform1,  VK_SHADER_STAGE_GEOMETRY_BIT);
    cube.draw(commandBuffer);

    camera->push(commandBuffer, shadow_volume_visual.layout, glm::rotate(glm::mat4(1), -glm::half_pi<float>(), {1, 0, 0}), VK_SHADER_STAGE_GEOMETRY_BIT);
    plane.draw(commandBuffer);
}

void ShadowVolumeDemo::renderScene(VkCommandBuffer commandBuffer) {

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout, 0, 1, &descriptorSet, 0, VK_NULL_HANDLE);
    camera->push(commandBuffer, render.layout, xform, VK_SHADER_STAGE_VERTEX_BIT);
    cube.draw(commandBuffer);

    camera->push(commandBuffer, shadow_volume.layout, xform1,  VK_SHADER_STAGE_GEOMETRY_BIT);
    cube.draw(commandBuffer);

    camera->push(commandBuffer, render.layout, glm::rotate(glm::mat4(1), -glm::half_pi<float>(), {1, 0, 0}), VK_SHADER_STAGE_VERTEX_BIT);
    plane.draw(commandBuffer);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ambient.pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ambient.layout, 0, 1, &descriptorSet, 0, VK_NULL_HANDLE);
    camera->push(commandBuffer, ambient.layout, xform, VK_SHADER_STAGE_VERTEX_BIT);
    cube.draw(commandBuffer);

    camera->push(commandBuffer, shadow_volume.layout, xform1,  VK_SHADER_STAGE_GEOMETRY_BIT);
    cube.draw(commandBuffer);

    camera->push(commandBuffer, ambient.layout, glm::rotate(glm::mat4(1), -glm::half_pi<float>(), {1, 0, 0}), VK_SHADER_STAGE_VERTEX_BIT);
    plane.draw(commandBuffer);

}

void ShadowVolumeDemo::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("SandBox app");
    ImGui::SetWindowSize({0, 0});
    ImGui::Text("%s", fmt::format("light {}", ubo->lightPosition).c_str());
    ImGui::Indent(16);

    static bool updateLight = true;
    static float elevation = 0;
    updateLight |= ImGui::SliderFloat("Elevation", &elevation, 0, glm::pi<float>());

    static float azimuth = 0;
    updateLight |= ImGui::SliderFloat("Azimuth", &azimuth, 0, glm::two_pi<float>());
    ImGui::Indent(-16);

    if(updateLight){
        updateLight = false;
        static float radius = 10;
        ubo->lightPosition.x = radius * glm::sin(elevation) * glm::sin(azimuth);
        ubo->lightPosition.y = radius * glm::cos(elevation);
        ubo->lightPosition.z = radius * glm::sin(elevation) * glm::cos(azimuth);
    }

    ImGui::Checkbox("Show shadow volume", &showShadowVolume);
    ImGui::Checkbox("show silhouette", &showSilhouette);

    ImGui::End();
    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void ShadowVolumeDemo::onSwapChainDispose() {

}

void ShadowVolumeDemo::onSwapChainRecreation() {
    camera->onResize(width, height);
    createPipeline();
}


void ShadowVolumeDemo::newFrame() {
    VulkanBaseApp::newFrame();
}

void ShadowVolumeDemo::endFrame() {
    VulkanBaseApp::endFrame();
}

void ShadowVolumeDemo::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
        ubo->cameraPosition = camera->position();
    }
}

void ShadowVolumeDemo::checkAppInputs() {
    camera->processInput();
}

void ShadowVolumeDemo::cleanup() {
    VulkanBaseApp::cleanup();
}

void ShadowVolumeDemo::onPause() {
    VulkanBaseApp::onPause();
}

void ShadowVolumeDemo::createPipeline() {
    //    @formatter:off
    VkPipelineColorWriteCreateInfoEXT colorWriteInfo{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_WRITE_CREATE_INFO_EXT };
    VkBool32 enableCW = VK_TRUE;
    colorWriteInfo.attachmentCount = 1;
    colorWriteInfo.pColorWriteEnables = &enableCW;

    auto builder = device.graphicsPipelineBuilder();
    depthOnly.pipeline =
        builder
            .allowDerivatives()
            .shaderStage()
                .vertexShader(resource("null.vert.spv"))
                .fragmentShader(resource("null.frag.spv"))
            .vertexInputState()
                .addVertexBindingDescriptions(Vertex::bindingDisc())
                .addVertexAttributeDescriptions(Vertex::attributeDisc())
            .inputAssemblyState()
                .trianglesWithAdjacency()
            .viewportState()
                .viewport()
                    .origin(0, 0)
                    .dimension(swapChain.extent)
                    .minDepth(0)
                    .maxDepth(1)
                .scissor()
                    .offset(0, 0)
                    .extent(swapChain.extent)
                .add()
                .rasterizationState()
                    .cullNone()
                    .frontFaceCounterClockwise()
                    .polygonModeFill()
                .multisampleState()
                    .rasterizationSamples(settings.msaaSamples)
                .depthStencilState()
                    .enableDepthWrite()
                    .enableDepthTest()
                    .compareOpLess()
                    .minDepthBounds(0)
                    .maxDepthBounds(1)
                .stencilOpBack()
                    .failOpKeep()
                    .passOpKeep()
                    .depthFailOpReplace()
                    .compareOpAlways()
                    .compareMask(0Xff)
                    .writeMask(0Xff)
                    .reference(0x01)
                .stencilOpFront()
                    .failOpKeep()
                    .passOpKeep()
                    .depthFailOpReplace()
                    .compareOpAlways()
                    .compareMask(0Xff)
                    .writeMask(0xff)
                    .reference(0x01)
                .colorBlendState()
                    .attachment()
                    .add()
                .layout()
                    .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0 , sizeof(Camera))
                .renderPass(renderPass)
                .subpass(0)
                .name("null")
                .pipelineCache(pipelineCache)
            .build(depthOnly.layout);

    shadow_volume.pipeline =
        builder
            .basePipeline(depthOnly.pipeline)
            .shaderStage()
                .vertexShader(resource("shadow_volume.vert.spv"))
                .geometryShader(resource("shadow_volume.geom.spv"))
                .fragmentShader(resource("null.frag.spv"))
            .inputAssemblyState()
                .trianglesWithAdjacency()
            .rasterizationState()
                .cullNone()
                .frontFaceCounterClockwise()
                .polygonModeFill()
                .enableDepthClamp()
            .depthStencilState()
                .enableDepthTest()
                .disableDepthWrite()
                .enableStencilTest()
                .compareOpLess()
                .stencilOpBack()
                    .failOpKeep()
                    .passOpKeep()
                    .depthFailOpIncrementAndWrap()
                    .compareOpAlways()
                    .compareMask(0Xff)
                    .writeMask(0Xff)
                    .reference(0x00)
                .stencilOpFront()
                    .failOpKeep()
                    .passOpKeep()
                    .depthFailOpDecrementAndWrap()
                    .compareOpAlways()
                    .compareMask(0Xff)
                    .writeMask(0xff)
                    .reference(0x00)
            .layout().clear()
                .addDescriptorSetLayout(descriptorSetLayout)
                .addPushConstantRange(VK_SHADER_STAGE_GEOMETRY_BIT, 0 , sizeof(Camera))
            .name("shadow_volume")
        .build(shadow_volume.layout);

    shadow_volume_visual.pipeline =
        builder
            .shaderStage()
                .fragmentShader(resource("line.frag.spv"))
            .rasterizationState()
//                .polygonModeLine()
            .depthStencilState()
                .enableDepthWrite()
                .compareOpLess()
                .disableStencilTest()
            .colorBlendState()
                .attachment().clear()
                .enableBlend()
                .colorBlendOp().add()
                .alphaBlendOp().add()
                .srcColorBlendFactor().srcAlpha()
                .dstColorBlendFactor().oneMinusSrcAlpha()
                .srcAlphaBlendFactor().one()
                .dstAlphaBlendFactor().one()
            .add()
            .name("shadow_volume_visual")
        .build(shadow_volume_visual.layout);

    silhouette.pipeline =
        builder
            .shaderStage()
                .vertexShader(resource("shader.vert.spv"))
                .geometryShader(resource("silhouette.geom.spv"))
                .fragmentShader(resource("line.frag.spv"))
            .inputAssemblyState()
                .trianglesWithAdjacency()
            .rasterizationState()
                .lineWidth(5.0)
            .colorBlendState()
                .attachment().clear().add()
            .layout().clear()
                .addDescriptorSetLayout(descriptorSetLayout)
                .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT, 0 , sizeof(Camera))
            .name("silhouette")
        .build(silhouette.layout);


    render.pipeline =
        builder
            .basePipeline(depthOnly.pipeline)
            .shaderStage()
                .clear()
                .vertexShader(resource("shader.vert.spv"))
                .fragmentShader(resource("shader.frag.spv"))
            .inputAssemblyState()
                .trianglesWithAdjacency()
            .rasterizationState()
                .cullBackFace()
                .frontFaceCounterClockwise()
                .polygonModeFill()
                .disableDepthClamp()
            .depthStencilState()
                .disableDepthWrite()
                .enableDepthTest()
                .compareOpLessOrEqual()
                .enableStencilTest()
                .stencilOpBack()
                    .failOpKeep()
                    .passOpKeep()
                    .depthFailOpKeep()
                    .compareOpEqual()
                    .compareMask(0xff)
                    .writeMask(0xff)
                    .reference(0x00)
                .stencilOpFront()
                    .failOpKeep()
                    .passOpKeep()
                    .depthFailOpKeep()
                    .compareOpEqual()
                    .compareMask(0xff)
                    .writeMask(0xff)
                    .reference(0x00)
                .colorBlendState()
                    .attachment().clear().add()
            .layout().clear()
                .addDescriptorSetLayout(descriptorSetLayout)
                .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0 , sizeof(Camera))
            .name("render")
        .build(render.layout);


    ambient.pipeline =
        builder
            .shaderStage()
                .fragmentShader(resource("ambient.frag.spv"))
            .depthStencilState()
                .disableStencilTest()
            .colorBlendState()
                .attachment().clear()
                .enableBlend()
                .colorBlendOp().add()
                .alphaBlendOp().add()
                .srcColorBlendFactor().one()
                .dstColorBlendFactor().one()
                .srcAlphaBlendFactor().one()
                .dstAlphaBlendFactor().one()
            .add()
            .name("ambient_light")
        .build(ambient.layout);

    //    @formatter:on

}

void ShadowVolumeDemo::initBuffers() {
    auto lcube = primitives::cube();
//    auto lcube = primitives::sphere(100, 100, 1.0, glm::mat4(1), randomColor(), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    auto lplane = primitives::plane(100, 100, 1000, 1000, glm::mat4{1}, glm::vec4{1}, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);


//    decltype(lcube.indices) unstripedIndex;
//    unstripedIndex.resize(lcube.indices.size() * 3);
//    auto count = meshopt_unstripify(unstripedIndex.data(), lcube.indices.data(), lcube.indices.size(), primitives::RESTART_PRIMITIVE);
//    lcube.indices.clear();
//    for(int i = 0; i < count; i++){
//        lcube.indices.push_back(unstripedIndex[i]);
//    }

    decltype(lcube.indices) adjIndex;
    adjIndex.resize(lcube.indices.size() * 2);
    meshopt_generateAdjacencyIndexBuffer(adjIndex.data(), lcube.indices.data(), lcube.indices.size(),
                                         reinterpret_cast<const float*>(lcube.vertices.data()), lcube.vertices.size(),
                                         sizeof(Vertex));
    lcube.indices = adjIndex;

    adjIndex.resize(lplane.indices.size() * 2);
    meshopt_generateAdjacencyIndexBuffer(adjIndex.data(), lplane.indices.data(), lplane.indices.size(),
                                         reinterpret_cast<const float*>(lplane.vertices.data()), lplane.vertices.size(),
                                         sizeof(Vertex));

    lplane.indices = adjIndex;

    std::vector<mesh::Mesh> meshes(1);
    meshes[0].vertices = lcube.vertices;
    meshes[0].indices = lcube.indices;
    phong::load(device, descriptorPool, cube, meshes);

    meshes[0].vertices = lplane.vertices;
    meshes[0].indices = lplane.indices;
    phong::load(device, descriptorPool, plane, meshes);

    phong::VulkanDrawableInfo info{};
    info.generateAdjacencyTriangles = true;
    phong::load(R"(C:\Users\Josiah Ebhomenye\OneDrive\media\models\ChineseDragon.obj)", device, descriptorPool, model, {}, true, 2);


}

void ShadowVolumeDemo::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 16> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
                    { VK_DESCRIPTOR_TYPE_SAMPLER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 100 * maxSets }
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void ShadowVolumeDemo::createDescriptorSet() {
    descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("main")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(ALL_SHADER_STAGES)
        .createLayout();
}

void ShadowVolumeDemo::updateDescriptorSet() {
    auto sets = descriptorPool.allocate( {descriptorSetLayout} );
    descriptorSet = sets[0];
    
    auto writes = initializers::writeDescriptorSets();
    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uboInfo{ uboBuffer, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &uboInfo;

    device.updateDescriptorSets(writes);
}

void ShadowVolumeDemo::initCamera() {
    FirstPersonSpectatorCameraSettings settings{};
    settings.rotationSpeed = 0.1f;
    settings.zNear = 1.0f;
    settings.zFar = 200.0f;
    settings.fieldOfView = 45.0f;
    settings.aspectRatio = static_cast<float>(swapChain.extent.width)/static_cast<float>(swapChain.extent.height);
    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), settings);
    camera->lookAt(glm::vec3(0, 1.5, 3), glm::vec3(0, 1.5, 0), {0, 1, 0});

}

void ShadowVolumeDemo::initUBO() {
    uboBuffer = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(UBO));
    ubo = reinterpret_cast<UBO*>(uboBuffer.map());

    ubo->lightPosition = {0, 1, 0};
}



int main(){
    Settings settings;
    settings.depthTest = true;
    settings.stencilTest = true;
    settings.enabledFeatures.wideLines = VK_TRUE;
    settings.enabledFeatures.geometryShader = VK_TRUE;
    settings.enabledFeatures.depthClamp = VK_TRUE;
    settings.enabledFeatures.fillModeNonSolid = VK_TRUE;

    try {
        auto app = ShadowVolumeDemo{settings};
        std::unique_ptr<Plugin> plugin = std::make_unique<ImGuiPlugin>();
        app.addPlugin(plugin);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}