#include "ImGuiPlugin.hpp"
#include "Demo.h"

#include <memory>
#include "Mesh.h"
#include "ImGuiPlugin.hpp"

Demo::Demo(const Settings &settings)
    : VulkanBaseApp("Demo", settings,  {})
{

    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/data");
    
    actions.help = &mapToKey(Key::H, "Help menu", Action::detectInitialPressOnly());
    actions.toggleVSync = &mapToKey(Key::V, "Toggle VSync", Action::detectInitialPressOnly());
    actions.fullscreen = &mapToKey(Key::ENTER, "Fullscreen", Action::detectInitialPressOnly(), KeyModifierFlagBits::ALT);
    cameraModes.flight = &mapToKey(Key::_1, "First person camera", Action::detectInitialPressOnly());
    cameraModes.spectator = &mapToKey(Key::_2, "Spectator camera", Action::detectInitialPressOnly());
    cameraModes.flight = &mapToKey(Key::_3, "Flight Camera", Action::detectInitialPressOnly());
    cameraModes.orbit = &mapToKey(Key::_4, "Orbit Camera",Action::detectInitialPressOnly());
}

void Demo::initApp() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
    createDescriptorPool();
    loadFloor();
    loadSpaceShip();
    createPipelines();
    initCamera();
    //msaaSamples = settings.msaaSamples;
    msaaSamples = device.getMaxUsableSampleCount();
    maxAnisotrophy = device.getLimits().maxSamplerAnisotropy;
}

void Demo::createDescriptorPool() {
    auto maxSets = 1000u;
    std::vector<VkDescriptorPoolSize> poolSizes{
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * maxSets}
    };
    auto phongPoolSizes = phong::getPoolSizes(maxSets);
    for(auto& poolSize : phongPoolSizes){
        auto itr = std::find_if(begin(poolSizes), end(poolSizes), [&](auto& p){
            return p.type == poolSize.type;
        });
        if(itr != end(poolSizes)){
            itr->descriptorCount += poolSize.descriptorCount;
        }else{
            poolSizes.push_back(poolSize);
        }
    }
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes);
}

void Demo::initCamera() {
    CameraSettings settings{};
    settings.aspectRatio = static_cast<float>(swapChain.extent.width)/static_cast<float>(swapChain.extent.height);
    settings.orbit.orbitMinZoom = CAMERA_ZOOM_MIN;
    settings.orbit.orbitMaxZoom = CAMERA_ZOOM_MAX;
    settings.acceleration = CAMERA_ACCELERATION;
    settings.velocity = CAMERA_VELOCITY;
    settings.orbit.offsetDistance = CAMERA_ZOOM_MIN + (CAMERA_ZOOM_MAX - CAMERA_ZOOM_MIN) * 0.3F;
    settings.rotationSpeed = 0.1f;
    settings.fieldOfView = CAMERA_FOVX;
    settings.zNear = CAMERA_ZNEAR;
    settings.zFar = CAMERA_ZFAR;
    settings.horizontalFov = true;
    settings.orbit.modelHeight = spaceShip.height();
//    CameraController(const VulkanDevice& device, uint32_t swapChainImageCount
//            , const uint32_t& currentImageInde, InputManager& inputManager
//            , const CameraSettings& settings);
    cameraController = std::make_unique<CameraController>(dynamic_cast<InputManager&>(*this), settings);
    cameraController->setMode(CameraMode::ORBIT);
}

void Demo::loadSpaceShip() {
    phong::load(resource("models/bigship1.obj"), device, descriptorPool, spaceShip);
}


void Demo::loadFloor() {
    auto flip90Degrees = glm::rotate(glm::mat4{1}, -glm::half_pi<float>(), {1, 0, 0});
    auto plane = primitives::plane(5, 5, floor.dimensions.width, floor.dimensions.height, flip90Degrees, glm::vec4(1));

    floor.vertices = device.createDeviceLocalBuffer(plane.vertices.data(), sizeof(Vertex) * plane.vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    floor.indices = device.createDeviceLocalBuffer(plane.indices.data(), sizeof(uint32_t) * plane.indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    floor.indexCount =  plane.indices.size();
    textures::fromFile(device, floor.texture, resource("textures/floor_color_map.tga"), true, VK_FORMAT_R8G8B8A8_SRGB);
    textures::fromFile(device, floor.lightMap, resource("textures/floor_light_map.tga"));
    createFloorDescriptors();
}

void Demo::createFloorDescriptors() {
    assert(descriptorPool != VK_NULL_HANDLE);

    std::vector<VkDescriptorSetLayoutBinding> bindings(2);
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    floor.descriptorSetLayout = device.createDescriptorSetLayout(bindings);
    floor.descriptorSet = descriptorPool.allocate({ floor.descriptorSetLayout }).front();

    std::array<VkWriteDescriptorSet, 2> writes{};

    VkDescriptorImageInfo imageInfo0{ floor.texture.sampler.handle, floor.texture.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[0] = initializers::writeDescriptorSet();
    writes[0].dstSet = floor.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].dstArrayElement = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &imageInfo0;

    VkDescriptorImageInfo imageInfo1{ floor.lightMap.sampler.handle, floor.lightMap.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[1] = initializers::writeDescriptorSet();
    writes[1].dstSet = floor.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].dstArrayElement = 0;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &imageInfo1;

    vkUpdateDescriptorSets(device, COUNT(writes), writes.data(), 0, VK_NULL_HANDLE);

}

void Demo::createPipelines() {
    auto vertexShaderModule = device.createShaderModule(resource("shaders/demo/floor.vert.spv"));
    auto fragmentShaderModule = device.createShaderModule(resource("shaders/demo/floor.frag.spv"));

    auto shaderStages = initializers::vertexShaderStages(
            {
                    {vertexShaderModule, VK_SHADER_STAGE_VERTEX_BIT},
                    {fragmentShaderModule, VK_SHADER_STAGE_FRAGMENT_BIT}});

    auto vertexBindings = Vertex::bindingDisc();
    auto vertexAttribs = Vertex::attributeDisc();

    auto vertexInputState = initializers::vertexInputState(vertexBindings, vertexAttribs);

    auto viewport = initializers::viewport(swapChain.extent);
    auto scissor = initializers::scissor(swapChain.extent);

    auto viewportState = initializers::viewportState(viewport, scissor);

    auto inputAssemblyState = initializers::inputAssemblyState();
    inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    inputAssemblyState.primitiveRestartEnable = VK_TRUE;

    auto rasterState = initializers::rasterizationState();
    rasterState.polygonMode = VK_POLYGON_MODE_FILL;
    rasterState.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    auto multisampleState = initializers::multisampleState();
    multisampleState.rasterizationSamples = settings.msaaSamples;
    multisampleState.sampleShadingEnable = VK_TRUE;
    multisampleState.minSampleShading = 0.2f;

    auto depthStencilState = initializers::depthStencilState();
    depthStencilState.depthTestEnable = VK_TRUE;
    depthStencilState.depthWriteEnable = VK_TRUE;
    depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencilState.minDepthBounds = 0;
    depthStencilState.maxDepthBounds = 1;

    std::vector<VkPipelineColorBlendAttachmentState> blendAttachmentState(1);
    blendAttachmentState[0].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT |
            VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT |
            VK_COLOR_COMPONENT_A_BIT;
    auto blendState = initializers::colorBlendState(blendAttachmentState);
    auto dynamicState = initializers::dynamicState();

    layouts.floorLayout = device.createPipelineLayout({ floor.descriptorSetLayout }, { Camera::pushConstant() });

    auto createInfo = initializers::graphicsPipelineCreateInfo();
    createInfo.flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
    createInfo.stageCount = COUNT(shaderStages);
    createInfo.pStages = shaderStages.data();
    createInfo.pVertexInputState = &vertexInputState;
    createInfo.pInputAssemblyState = &inputAssemblyState;
    createInfo.pTessellationState = nullptr;
    createInfo.pViewportState = &viewportState;
    createInfo.pRasterizationState = &rasterState;
    createInfo.pMultisampleState = &multisampleState;
    createInfo.pDepthStencilState = &depthStencilState;
    createInfo.pColorBlendState = &blendState;
    createInfo.pDynamicState = &dynamicState;
    createInfo.layout = layouts.floorLayout.handle;
    createInfo.renderPass = renderPass;
    createInfo.subpass = 0;
    createInfo.basePipelineIndex = -1;
    createInfo.basePipelineHandle = VK_NULL_HANDLE;

    pipelines.floor = device.createGraphicsPipeline(createInfo);

    auto vertexShaderModule1 = device.createShaderModule(resource("shaders/demo/spaceship.vert.spv"));
    auto fragmentShaderModule1 = device.createShaderModule(resource("shaders/demo/spaceship.frag.spv"));

    shaderStages = initializers::vertexShaderStages(
            {
                    {vertexShaderModule1, VK_SHADER_STAGE_VERTEX_BIT},
                    {fragmentShaderModule1, VK_SHADER_STAGE_FRAGMENT_BIT}});

    inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssemblyState.primitiveRestartEnable = VK_FALSE;

    layouts.spaceShipLayout = device.createPipelineLayout({ spaceShip.descriptorSetLayout}, {Camera::pushConstant()});

    createInfo.flags = VK_PIPELINE_CREATE_DERIVATIVE_BIT;
    createInfo.pStages = shaderStages.data();
    createInfo.layout = layouts.spaceShipLayout.handle;
    createInfo.pInputAssemblyState = &inputAssemblyState;
    createInfo.basePipelineIndex = -1;
    createInfo.basePipelineHandle = pipelines.floor.handle;

    pipelines.spaceShip = device.createGraphicsPipeline(createInfo);

}

void Demo::onSwapChainDispose() {
    dispose(layouts.spaceShipLayout);
    dispose(layouts.floorLayout);
    dispose(pipelines.spaceShip);
    dispose(pipelines.floor);
}

void Demo::onSwapChainRecreation() {
    cameraController->onResize(width, height);
    createPipelines();
}

VkCommandBuffer *Demo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    auto cbBeginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &cbBeginInfo);

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    clearValues[1].depthStencil = {1.0f, 0u};

    auto rpassBeginInfo = initializers::renderPassBeginInfo();
    rpassBeginInfo.renderPass = renderPass;
    rpassBeginInfo.framebuffer = framebuffers[imageIndex];
    rpassBeginInfo.renderArea.offset = {0u, 0u};
    rpassBeginInfo.renderArea.extent = swapChain.extent;
    rpassBeginInfo.clearValueCount = COUNT(clearValues);
    rpassBeginInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &rpassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.floor.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layouts.floorLayout.handle, 0, 1, &floor.descriptorSet, 0, VK_NULL_HANDLE);
    cameraController->push(commandBuffer, layouts.floorLayout, glm::mat4(1));
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, floor.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, floor.indices, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, floor.indexCount, 1, 0, 0, 0);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.spaceShip.handle);


    if(cameraController->isInObitMode()) {
        cameraController->push(commandBuffer, layouts.spaceShipLayout);
        spaceShip.draw(commandBuffer, layouts.spaceShipLayout);
    }

    displayMenu(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);
    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void Demo::update(float time) {
    cameraController->update(time);
}

void Demo::checkAppInputs() {
    if(actions.help->isPressed()){
        displayHelp = !displayHelp;
    }
    if(actions.toggleVSync->isPressed()){
        settings.vSync = !settings.vSync;
        swapChainInvalidated = true;
    }
    if(actions.fullscreen->isPressed()){
        toggleFullscreen = true;
    }

    cameraController->processInput();
}

void Demo::cleanup() {

}

void Demo::displayMenu(VkCommandBuffer commandBuffer) {
    auto& imgui = plugin<ImGuiPlugin>(IM_GUI_PLUGIN);
    auto font = imgui.font("Arial", 15);
    ImGui::Begin("Menu", nullptr, IMGUI_NO_WINDOW);
    ImGui::SetWindowSize({ 500, float(height)});
    ImGui::PushFont(font);
    ImGui::TextColored({1, 1, 0, 1}, menu().c_str());
    ImGui::PopFont();
    ImGui::End();
    imgui.draw(commandBuffer);
}

std::string Demo::menu() const {
    static std::stringstream ss;
    ss.clear();
    ss.str("");
    if(displayHelp){
        ss
            << "Press 1 to switch to first person behavior\n"
            << "Press 2 to switch to spectator behavior\n"
            << "Press 3 to switch to flight behavior\n"
            << "Press 4 to switch to orbit behavior\n\n"
            << "First Person and Spectator behaviors\n"
            << "\tPress W and S to move forwards and backwards\n"
            << "\tPress A and D to strafe left and right\n"
            << "\tPress E and Q to move up and down\n"
            << "\tMove mouse to free look\n\n"
            << "Flight behavior\n"
            << "\tPress W and S to move forwards and backwards\n"
            << "\tPress A and D to yaw left and right\n"
            << "\tPress E and Q to move up and down\n"
            << "\tMove mouse up and down to change pitch\n"
            << "\tMove mouse left and right to change roll\n\n"
            << "Orbit behavior\n"
            << "\tPress SPACE to enable/disable target Y axis orbiting\n"
            << "\tMove mouse to orbit the model\n"
            << "\tMouse wheel to zoom in and out\n\n"
            << "Press M to enable/disable mouse smoothing\n"
            << "Press V to enable/disable vertical sync\n"
            << "Press + and - to change camera rotation speed\n"
            << "Press , and . to change mouse sensitivity\n"
            << "Press BACKSPACE or middle mouse button to level camera\n"
            << "Press ALT and ENTER to toggle full screen\n"
            << "Press ESC to exit\n\n"
            << "Press H to hide help";

        return ss.str();
    }else{
        auto currentMode = cameraController->mode();
        auto orbitStyle = "";
        auto rotationSpeed = "";
        auto verticalSync = settings.vSync ? "enabled" : "disabled";
        auto mouseSmoothing = "disabled";
        auto mouseSensivitiy = 0;
        auto position = cameraController->position();
        auto velocity = cameraController->velocity();

        ss
            << "FPS: {}\n"
            << "Multisample anti-aliasing: {}x\n"
            << "Anisotropic filtering: {}x\n"
            << "Vertical sync: {}\n\n"
            << "Camera\n"
            << "\tPosition: {}\n"
            << "\tVelocity: {}\n"
            << "\tMode: {}\n"
            << "\tRotation Speed: {}\n"
            << "\tOrbit Style: {}\n\n"
            << "Mouse\n"
            << "\tSmoothing: {}\n"
            << "\tSensitivity: {}\n\n"
            << "Press H to display help";

        return fmt::format(ss.str(), framePerSecond, msaaSamples, maxAnisotrophy, verticalSync, position, velocity
                                   , currentMode, rotationSpeed, orbitStyle, mouseSmoothing, mouseSensivitiy);
    }
}


int main(){
    try{
        Settings settings;
        settings.depthTest = true;
        settings.relativeMouseMode = true;
        settings.vSync = false;
        settings.fullscreen = false;
        settings.surfaceFormat.format = VK_FORMAT_B8G8R8A8_SRGB;
        settings.width = 2560 * 0.7;
        settings.height = 720;
        settings.msaaSamples = VK_SAMPLE_COUNT_8_BIT;
        settings.enabledFeatures.sampleRateShading = VK_TRUE;


        std::vector<FontInfo> fonts {
#ifdef WIN32
                {"JetBrainsMono", R"(C:\Users\Josiah Ebhomenye\CLionProjects\vulkan_bootstrap\data\fonts\JetBrainsMono\JetBrainsMono-Regular.ttf)", 20},
                {"Arial", R"(C:\Windows\Fonts\arial.ttf)", 20},
                {"Arial", R"(C:\Windows\Fonts\arial.ttf)", 15}
#elif defined(__APPLE__)
                {"Arial", "/Library/Fonts/Arial Unicode.ttf", 20},
                {"Arial", "/Library/Fonts/Arial Unicode.ttf", 15}
#endif
        };
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>(fonts);

        auto app = Demo{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}