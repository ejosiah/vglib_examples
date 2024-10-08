#include "SkeletalAnimationDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"

SkeletalAnimationDemo::SkeletalAnimationDemo(const Settings& settings) : VulkanBaseApp("Skeletal Animation", settings) {
    fileManager.addSearchPath(".");
    fileManager.addSearchPath("../../examples/skeletal_animation");
    fileManager.addSearchPath("../../examples/skeletal_animation/spv");
    fileManager.addSearchPath("../../examples/skeletal_animation/models");
    fileManager.addSearchPath("../../examples/skeletal_animation/textures");
    fileManager.addSearchPath("../../data/shaders");
    fileManager.addSearchPath("../../data/models");
    fileManager.addSearchPath("../../data/textures");
    fileManager.addSearchPath("../../data");
    jump = &mapToKey(Key::SPACE_BAR, "jump", Action::detectInitialPressOnly());
    dance = &mapToKey(Key::O, "dance", Action::detectInitialPressOnly());
    walk = &mapToKey(Key::M, "walk", Action::detectInitialPressOnly());
}

void SkeletalAnimationDemo::initApp() {
    createDescriptorPool();
    createCommandPool();
    initModel();
    initCamera();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
}

void SkeletalAnimationDemo::createDescriptorPool() {
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

void SkeletalAnimationDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void SkeletalAnimationDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void SkeletalAnimationDemo::createRenderPipeline() {
    auto builder = device.graphicsPipelineBuilder();
    render.pipeline =
        builder
            .allowDerivatives()
            .shaderStage()
                .vertexShader(load("render.vert.spv"))
                .fragmentShader(load("render.frag.spv"))
            .vertexInputState()
                .addVertexBindingDescriptions(Vertex::bindingDisc())
                .addVertexBindingDescription(1, sizeof(mdl::VertexBoneInfo), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescriptions(Vertex::attributeDisc())
                .addVertexAttributeDescription(6, 1, VK_FORMAT_R32G32B32A32_UINT, (size_t)&(((mdl::VertexBoneInfo*)0)->boneIds[0]))
                .addVertexAttributeDescription(7, 1, VK_FORMAT_R32G32B32A32_UINT, (size_t)&(((mdl::VertexBoneInfo*)0)->boneIds[4]))
                .addVertexAttributeDescription(8, 1, VK_FORMAT_R32G32B32A32_SFLOAT, (size_t)&(((mdl::VertexBoneInfo*)0)->weights[0]))
                .addVertexAttributeDescription(9, 1, VK_FORMAT_R32G32B32A32_SFLOAT, (size_t)&(((mdl::VertexBoneInfo*)0)->weights[4]))
            .inputAssemblyState()
                .triangles()
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
                    .cullBackFace()
                    .frontFaceCounterClockwise()
                    .polygonModeFill()
                .depthStencilState()
                    .enableDepthWrite()
                    .enableDepthTest()
                    .compareOpLess()
                    .minDepthBounds(0)
                    .maxDepthBounds(1)
                .colorBlendState()
                    .attachment()
                    .add()
                .layout()
                    .addPushConstantRange(Camera::pushConstant())
                    .addDescriptorSetLayouts({ model->descriptor.boneLayout, model->descriptor.materialLayout })
                .renderPass(renderPass)
                .subpass(0)
                .name("render")
                .pipelineCache(pipelineCache)
            .build(render.layout);

    outline.pipeline =
        builder
            .basePipeline(render.pipeline)
            .shaderStage()
                .vertexShader(load("outline.vert.spv"))
                .fragmentShader(load("outline.frag.spv"))
            .rasterizationState()
                .frontFaceClockwise()
                .cullBackFace()
            .layout()
                .clear()
                .addPushConstantRange(Camera::pushConstant())
                .addDescriptorSetLayout( model->descriptor.boneLayout)
            .name("outline")
        .build(outline.layout);

}

void SkeletalAnimationDemo::createComputePipeline() {
    auto module = device.createShaderModule( "../../data/shaders/pass_through.comp.spv");
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute.layout = device.createPipelineLayout();

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.layout.handle;

    compute.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
}


void SkeletalAnimationDemo::onSwapChainDispose() {
    dispose(render.pipeline);
    dispose(compute.pipeline);
}

void SkeletalAnimationDemo::onSwapChainRecreation() {
    initCamera();
    createRenderPipeline();
    createComputePipeline();
}

VkCommandBuffer *SkeletalAnimationDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    static std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = {0.4, 0.4, 0.4, 1};
    clearValues[1].depthStencil = {1.0, 0u};

    VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
    rPassInfo.clearValueCount = COUNT(clearValues);
    rPassInfo.pClearValues = clearValues.data();
    rPassInfo.framebuffer = framebuffers[imageIndex];
    rPassInfo.renderArea.offset = {0u, 0u};
    rPassInfo.renderArea.extent = swapChain.extent;
    rPassInfo.renderPass = renderPass;

    vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);


    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = model->descriptor.sets[0];
    sets[1] = model->descriptor.sets[1];


    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, outline.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, outline.layout.handle, 0, 1, sets.data(), 0, VK_NULL_HANDLE);
    cameraController->push(commandBuffer, outline.layout);
    model->render(commandBuffer);


    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    cameraController->push(commandBuffer, render.layout);
    model->render(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void SkeletalAnimationDemo::update(float time) {
    glfwSetWindowTitle(window, fmt::format("{} - FPS {}", title, framePerSecond).c_str());
    cameraController->update(time);
    vampire.update(time);
    if(vampire.currentState != "idle" && vampire.animations[vampire.currentState].finished()){
        vampire.animations[vampire.currentState].elapsedTime = 0;
        vampire.currentState  = "idle";
    }
}

void SkeletalAnimationDemo::checkAppInputs() {
    cameraController->processInput();

    if(jump->isPressed()){
        vampire.animations[vampire.currentState].elapsedTime = 0;
        vampire.currentState = "back_flip";
    }

    if(dance->isPressed()){
        vampire.animations[vampire.currentState].elapsedTime = 0;
        vampire.currentState = "dance";
    }

    if(walk->isPressed()){
        vampire.animations[vampire.currentState].elapsedTime = 0;
        vampire.currentState = "walk";
    }
}

void SkeletalAnimationDemo::cleanup() {
}

void SkeletalAnimationDemo::onPause() {
    VulkanBaseApp::onPause();
}

void walkBoneHierarchy1(const anim::Animation& animation, const anim::AnimationNode& node, int depth){
    const auto& nodes = animation.nodes;
    fmt::print("{: >{}}{}[{}, {}]\n", "", depth + 1, node.name, node.id, node.parentId);
    for(const auto& i : node.children){
        walkBoneHierarchy1(animation, nodes[i], depth + 1);
    }
}

void SkeletalAnimationDemo::initModel() {
    auto path = std::string{ "../../data/models/character/Wave_Hip_Hop_Dance.fbx" };
    auto path0 = std::string{ "../../data/models/character/Backflip.fbx" };
//    auto path = std::string{ "../../data/models/character/Walking.fbx" };
    model = mdl::load(device, path);
    model->updateDescriptorSet(device, descriptorPool);
    auto dance = anim::load(model.get(), "../../data/models/character/Wave_Hip_Hop_Dance.fbx" ).front();
    auto backFlip = anim::load(model.get(), "../../data/models/character/Backflip.fbx" ).front();
    auto idle = anim::load(model.get(), "../../data/models/character/Idle.fbx" ).front();
    auto walking = anim::load(model.get(), "../../data/models/character/Walking.fbx" ).front();
    dance.name = "dance";

    backFlip.name = "back_flip";
    backFlip.loop = false;

    idle.name = "idle";
    walking.name = "walk";
    vampire.animations["dance"] = dance;
    vampire.animations["back_flip"] = backFlip;
    vampire.animations["idle"] = idle;
    vampire.animations["walk"] = walking;
    vampire.currentState = "idle";
    spdlog::info("model bounds: [{}, {}], height: {}", model->bounds.min, model->bounds.max, model->height());
//    walkBoneHierarchy1(dance, dance.nodes[0], 0);
}

void SkeletalAnimationDemo::initCamera() {
//    OrbitingCameraSettings settings{};
//    settings.aspectRatio = static_cast<float>(swapChain.extent.width)/static_cast<float>(swapChain.extent.height);
//    settings.rotationSpeed = 0.1f;
//    settings.horizontalFov = true;
//    settings.zFar = 2000;
//    settings.offsetDistance = glm::max(model->diagonal().z, glm::max(model->diagonal().x, model->diagonal().y)) * 2;
//    settings.orbitMaxZoom = settings.offsetDistance;
//    settings.modelHeight = model->height();
//    settings.model.min = model->bounds.min;
//    settings.model.max = model->bounds.max;
//    cameraController = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), settings);

    CameraSettings settings{};
    settings.aspectRatio = static_cast<float>(swapChain.extent.width)/static_cast<float>(swapChain.extent.height);
    settings.rotationSpeed = 0.1f;
    settings.horizontalFov = true;
    settings.zNear = 1.0;
    settings.zFar = 2000;
    settings.velocity = glm::vec3(200);
    settings.acceleration = glm::vec3(100);
    cameraController = std::make_unique<CameraController>(dynamic_cast<InputManager&>(*this), settings);
    cameraController->setMode(CameraMode::SPECTATOR);
    auto target = (model->bounds.max + model->bounds.min) * 0.5f;
    auto pos = target + glm::vec3(0, 0, 1) * model->diagonal().z * 5.f;
    cameraController->lookAt(pos, target, {0, 1, 0});

}

void walkBoneHierarchy(const mdl::Model& model, const mdl::Bone& bone, int depth){
    const std::vector<mdl::Bone>& bones = model.bones;
    auto [min, max] = model.boneBounds[bone.id];
    fmt::print("{: >{}}{}[min: {}, max: {}]\n", "", depth + 1, bone.name, min, max);
    for(const auto& i : bone.children){
        walkBoneHierarchy(model, bones[i], depth + 1);
    }
}

int main(){
    try{

        Settings settings;
        settings.depthTest = true;

        auto app = SkeletalAnimationDemo{ settings };
        app.run();

    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}