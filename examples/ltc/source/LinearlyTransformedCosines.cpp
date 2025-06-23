#include "LinearlyTransformedCosines.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include "Barrier.hpp"
#include "ltc.hpp"

LinearlyTransformedCosines::LinearlyTransformedCosines(const Settings& settings) : VulkanBaseApp("Linearly Transformed Cosines", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/ltc");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("ltc");
    fileManager().addSearchPathFront("ltc/data");
    fileManager().addSearchPathFront("ltc/spv");
    fileManager().addSearchPathFront("ltc/models");
    fileManager().addSearchPathFront("ltc/textures");
}

void LinearlyTransformedCosines::initApp() {
    initUniforms();
    initCamera();
    createDescriptorPool();
    initBindlessDescriptor();
    loadLtcTextures();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    loadModel();
    createLightSource();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipelines();
    prefilterLightSource();
}

void LinearlyTransformedCosines::initCamera() {
//    OrbitingCameraSettings cameraSettings;
    FirstPersonSpectatorCameraSettings cameraSettings;
//    cameraSettings.orbitMinZoom = 0.1;
//    cameraSettings.orbitMaxZoom = 512.0f;
//    cameraSettings.offsetDistance = 1.0f;
//    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
}

void LinearlyTransformedCosines::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void LinearlyTransformedCosines::beforeDeviceCreation() {
    auto devFeatures12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    devFeatures12->scalarBlockLayout = VK_TRUE;
    devFeatures12->shaderOutputViewportIndex = VK_TRUE;

    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    auto dsFeatures = findExtension<VkPhysicalDeviceExtendedDynamicState3FeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT, deviceCreateNextChain);
    dsFeatures->extendedDynamicState3PolygonMode = VK_TRUE;

    auto indexType8 = findExtension<VkPhysicalDeviceIndexTypeUint8FeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT, deviceCreateNextChain);
    indexType8->indexTypeUint8 = VK_TRUE;
}

void LinearlyTransformedCosines::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 4> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets },
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}


void LinearlyTransformedCosines::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void LinearlyTransformedCosines::createDescriptorSetLayouts() {
    ltcDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("ltc_textures_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();
    
    uniformsDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("ltc_uniforms_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    textureDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("texture_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    imageDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("image_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();
}

void LinearlyTransformedCosines::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({ ltcDescriptorSetLayout, uniformsDescriptorSetLayout });
    ltcDescriptorSet = sets[0];
    uniformsDescriptorSet = sets[1];

    auto writes = initializers::writeDescriptorSets<4>();

    writes[0].dstSet = ltcDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo ltc_mat_info{ ltc_mat.sampler.handle, ltc_mat.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[0].pImageInfo = &ltc_mat_info;

    writes[1].dstSet = ltcDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo ltc_mag_info{ ltc_mag.sampler.handle, ltc_mag.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[1].pImageInfo = &ltc_mag_info;

    writes[2].dstSet = uniformsDescriptorSet;
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo uniformsInfo{ uniforms.gpu, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &uniformsInfo;

    writes[3].dstSet = uniformsDescriptorSet;
    writes[3].dstBinding = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[3].descriptorCount = 1;
    VkDescriptorBufferInfo lightPoints{ lightSource.points, 0, VK_WHOLE_SIZE };
    writes[3].pBufferInfo = &lightPoints;

    device.updateDescriptorSets(writes);
}

void LinearlyTransformedCosines::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void LinearlyTransformedCosines::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void LinearlyTransformedCosines::createRenderPipeline() {
    //    @formatter:off
        render.ltc.pipeline =
            prototypes->cloneScreenSpaceGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("quad.vert.spv"))
                    .fragmentShader(resource("ltc.frag.spv"))
                .layout()
                    .addDescriptorSetLayout(ltcDescriptorSetLayout)
                    .addDescriptorSetLayout(uniformsDescriptorSetLayout)
                .name("ltc_reference")
            .build(render.ltc.layout);

        render.light.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("flat.vert.spv"))
                    .fragmentShader(resource("light.frag.spv"))
                .inputAssemblyState()
                    .triangleStrip()
                .rasterizationState()
                    .cullNone()
                .layout()
                    .addDescriptorSetLayout(uniformsDescriptorSetLayout)
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                .name("light")
            .build(render.light.layout);

    render.pbr.pipeline =
        prototypes->cloneGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("render.vert.spv"))
                .fragmentShader(resource("render.frag.spv"))
            .vertexInputState().clear()
                .addVertexBindingDescription(VertexMultiAttributes::bindingDescription())
                .addVertexAttributeDescriptions(VertexMultiAttributes::attributeDescription())
            .rasterizationState()
                .cullNone()
            .colorBlendState()
                .attachment().clear()
                .enableBlend()
                .colorBlendOp().add()
                .alphaBlendOp().add()
                .srcColorBlendFactor().srcAlpha()
                .dstColorBlendFactor().oneMinusSrcAlpha()
                .srcAlphaBlendFactor().zero()
                .dstAlphaBlendFactor().one()
            .add()
            .layout()
                .addDescriptorSetLayout(loader->descriptorSetLayout())
                .addDescriptorSetLayout(loader->materialDescriptorSetLayout())
                .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                .addDescriptorSetLayout(uniformsDescriptorSetLayout)
            .name("forward_render")
        .build(render.pbr.layout);
    //    @formatter:on
}


void LinearlyTransformedCosines::onSwapChainDispose() {
    dispose(render.ltc.pipeline);
}

void LinearlyTransformedCosines::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *LinearlyTransformedCosines::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    renderToSwapChain([&]{
        if(scene == Scene::Reference) {
            renderReference(commandBuffer);
        }else {
            renderLightSource(commandBuffer);
            renderModel(commandBuffer);
        }
        renderControls(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void LinearlyTransformedCosines::update(float time) {
    if(scene == Scene::Reference){
        updateView();
    }else {
        if(!ImGui::IsAnyItemActive()) {
            camera->update(time);
        }
    }
}

void LinearlyTransformedCosines::endFrame() {
    static auto pScene = scene;
    if(pScene != scene) {
        if(scene == Scene::Reference) {
            *uniforms.cpu = reference;
        }else {
            *uniforms.cpu = lightSource.props;
        }
        pScene = scene;
    }else {
        if(scene == Scene::Reference) {
            reference = *uniforms.cpu;
         }else {
            lightSource.props = *uniforms.cpu;
            const auto p = lightSource.props;
            glm::mat4 pose{1};
            auto rotY = glm::mix(0.f, glm::two_pi<float>(), p.roty);
            auto rotZ = glm::mix(0.f, glm::two_pi<float>(), p.rotz);
            pose = glm::rotate(pose, rotZ, {0, 0, 1});
            pose = glm::rotate(pose, rotY, {0, 1, 0});
            pose = glm::scale(pose, {p.width, p.height, 1});
            lightSource.pose = pose;
            uniforms.cpu->view = lightSource.worldTransform * pose;
            uniforms.cpu->eyes = camera->position();

        }
    }
}

void LinearlyTransformedCosines::checkAppInputs() {
    if(scene == Scene::Reference){
        static bool initialPress = true;
        static glm::vec2 pos{};

        if(!ImGui::IsAnyItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if(initialPress) {
                initialPress = false;
                pos.x = ImGui::GetMousePos().x;
                pos.y = ImGui::GetMousePos().y;
            }
            cam.rotY = ImGui::GetMousePos().x - pos.x;
            cam.rotX = ImGui::GetMousePos().y - pos.y;
        }else {
            initialPress = true;
            pos = glm::vec2(0);
        }
        cam.zoom += 10 * ImGui::GetIO().MouseWheel;
    }else {
        if(!ImGui::IsAnyItemActive()) {
            camera->processInput();
        }
    }
}

void LinearlyTransformedCosines::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void LinearlyTransformedCosines::onPause() {
    VulkanBaseApp::onPause();
}

void LinearlyTransformedCosines::loadLtcTextures() {
    
    auto loadTexture = [this](auto& texture, auto path, auto w, auto h, auto format) {
        textures::createNoTransition(device, texture, VK_IMAGE_TYPE_2D, format, {w, h, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        auto stagingBuffer = device.createStagingBuffer(w * h * sizeof(float) * 4);
        auto data = load(path);
        stagingBuffer.copy(data);
        
        device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
            Barriers::push(texture.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_NONE, 
                           VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL); 
            
            Barriers::flush(commandBuffer);

            VkBufferImageCopy region{
                .imageSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                },
                .imageOffset = {0, 0, 0},
                .imageExtent = {w, h, 1}
            };
            vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            Barriers::push(texture.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            Barriers::flush(commandBuffer);
        });
        
    };

    loadTexture(ltc_mag, "ltc_amp.dat", 64u, 64u, VK_FORMAT_R32G32_SFLOAT);
    loadTexture(ltc_mat, "ltc_mat.dat", 64u, 64u, VK_FORMAT_R32G32B32A32_SFLOAT);
    bindlessDescriptor.update({&ltc_mag, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0});
    bindlessDescriptor.update({&ltc_mat, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1});
}

void LinearlyTransformedCosines::initUniforms() {
    LtcUniforms defaultValues{ .resolution = {width, height} };
    
    uniforms.gpu = device.createCpuVisibleBuffer(&defaultValues, sizeof(defaultValues), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    uniforms.cpu = as<LtcUniforms>(uniforms.gpu.map());
}

void LinearlyTransformedCosines::updateView() {
    glm::mat4 view{1};
    view = glm::translate(view, {0, 6, 0.1 * cam.zoom - 0.5});
    view = glm::rotate(view, glm::radians(cam.rotX - 10.f), {1, 0, 0});
    view = glm::rotate(view, glm::radians(cam.rotY), {0, 1, 0});
    view = glm::scale(view, {1, -1, 1});
    uniforms.cpu->view = view;
}

void LinearlyTransformedCosines::renderControls(VkCommandBuffer commandBuffer) {

    auto& ltc = uniforms.cpu;

    ImGui::Begin("LTC");
    ImGui::SetWindowSize({0, 0});

    static int iScene = to<int>(scene);
    ImGui::Text("Scene: ");
    ImGui::SameLine();
    ImGui::RadioButton("Reference", &iScene, to<int>(Scene::Reference));
    ImGui::SameLine();
    ImGui::RadioButton("Sponza", &iScene, to<int>(Scene::Sponza));

    ImGui::SliderFloat("Roughness", &ltc->roughness, 0, 1);
    ImGui::ColorEdit3("Diffuse Color", &ltc->dcolor.r);
    ImGui::ColorEdit3("Specular Color", &ltc->scolor.r);

    static float intensity = ltc->intensity;
    ImGui::SliderFloat("Light Intensity", &intensity, 0, 10);
    ImGui::SliderFloat("Width", &ltc->width, 0.1, 15);
    ImGui::SliderFloat("Height", &ltc->height, 0.1, 15);
    ImGui::SliderFloat("Rotation Y", &ltc->roty, 0, 1);
    ImGui::SliderFloat("Rotation Z", &ltc->rotz, 0, 1);

    static bool twoSided = bool(ltc->twoSided);
    ImGui::Checkbox("Two Sided", &twoSided);
    ltc->twoSided = int(twoSided);
    ImGui::End();

    scene = to<Scene>(iScene);
    ltc->intensity = intensity;

    if(scene == Scene::Sponza) {
        ltc->intensity = intensity * 100;
    }

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void LinearlyTransformedCosines::loadModel() {
    model = loader->loadGltf(resource("Sponza/glTF/Sponza.gltf"));
    model->sync();
}

void LinearlyTransformedCosines::renderModel(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = model->meshDescriptorSet.u16.handle;
    sets[1] = model->materialDescriptorSet;
    sets[2] = bindlessDescriptor.descriptorSet;
    sets[3] = uniformsDescriptorSet;

    VkDeviceSize offset = 0;
    camera->push(commandBuffer, render.pbr.layout);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pbr.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pbr.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, model->vertices, &offset);

//    vkCmdSetColorBlendEnableEXT(commandBuffer, 0, 1, &blendingEnabled);
    vkCmdBindIndexBuffer(commandBuffer, model->indices.u16.handle, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexedIndirect(commandBuffer, model->draw.u16.handle, 0, model->draw.u16.count, sizeof(VkDrawIndexedIndirectCommand));

    sets[0] = model->meshDescriptorSet.u32.handle;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pbr.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindIndexBuffer(commandBuffer, model->indices.u32.handle, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexedIndirect(commandBuffer, model->draw.u32.handle, 0, model->draw.u32.count, sizeof(VkDrawIndexedIndirectCommand));

    sets[0] = model->meshDescriptorSet.u8.handle;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pbr.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindIndexBuffer(commandBuffer, model->indices.u8.handle, 0, VK_INDEX_TYPE_UINT8_EXT);
    vkCmdDrawIndexedIndirect(commandBuffer, model->draw.u8.handle, 0, model->draw.u8.count, sizeof(VkDrawIndexedIndirectCommand));
}

void LinearlyTransformedCosines::renderReference(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = ltcDescriptorSet;
    sets[1] = uniformsDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.ltc.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.ltc.layout.handle, 0, COUNT(sets), sets.data(), 0, nullptr);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void LinearlyTransformedCosines::createLightSource() {
    const auto p = (model->bounds.max + model->bounds.min)/2.f;
    const auto d = (model->bounds.max - model->bounds.min) * .28f;
    glm::mat4 xform{1};
    xform = glm::translate(xform, {p.x + d.x, 1.5, p.z});
    xform = glm::rotate(xform, glm::half_pi<float>(), {0, 1, 0});
    lightSource.worldTransform = xform;

    auto quad = primitives::plane(1, 1, 1, 1, glm::mat4{1}, glm::vec4{1});
    lightSource.vertices = device.createDeviceLocalBuffer(quad.vertices.data(), BYTE_SIZE(quad.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    lightSource.indices = device.createDeviceLocalBuffer(quad.indices.data(), BYTE_SIZE(quad.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    std::vector<glm::vec3> points{};
    for(auto v : quad.vertices) {
        points.push_back(v.position.xyz());
    }
    lightSource.points = device.createDeviceLocalBuffer(points.data(), BYTE_SIZE(points), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    lightSource.props.width = 1;
    lightSource.props.height = 1;
    auto filename = "rainbow-pattern.jpg";
    int w, h, comp;
    stbi_info(resource(filename).c_str(), &w, &h, &comp);
//    w = 1024; h = 1024;
    const int size = std::max(w, h);
    const auto levels = to<int>(std::log2(size) + 1);

    lightSource.texture.levels = levels;
    textures::fromFile(device, lightSource.texture, resource(filename), true, VK_FORMAT_R8G8B8A8_SRGB, levels);
//    textures::color(device, lightSource.texture, {1, 0, 0}, {1024, 1024});
    textures::generateLOD(device, lightSource.texture, levels);

    lightSource.prefiltered.levels = levels;
    textures::fromFile(device, lightSource.prefiltered, resource(filename), true, VK_FORMAT_R8G8B8A8_UNORM, levels);
//    textures::color(device, lightSource.prefiltered, {1, 0, 0}, {1024, 1024});
    bindlessDescriptor.update({ &lightSource.texture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 });
    bindlessDescriptor.update({ &lightSource.prefiltered, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 });
}

void LinearlyTransformedCosines::renderLightSource(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[1] = bindlessDescriptor.descriptorSet;
    sets[0] = uniformsDescriptorSet;

    VkDeviceSize offset = 0;
    auto model = lightSource.worldTransform * lightSource.pose;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.light.pipeline.handle);
    camera->push(commandBuffer, render.light.layout, model);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.light.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, lightSource.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, lightSource.indices, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, 4, 1, 0, 0, 0);
}

void LinearlyTransformedCosines::createComputePipelines() {
    auto module = device.createShaderModule( resource("prefilter.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute.prefilter.layout = device.createPipelineLayout({{textureDescriptorSetLayout, imageDescriptorSetLayout, ltcDescriptorSetLayout}},
                                                           { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(compute.prefilter.constants)} });

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.prefilter.layout.handle;

    compute.prefilter.pipeline = device.createComputePipeline(computeCreateInfo);
}

void LinearlyTransformedCosines::prefilterLightSource() {
    std::vector<VulkanImageView> imageViews{};
    imageViews.push_back(lightSource.prefiltered.imageView);
    device.setName<VK_OBJECT_TYPE_IMAGE_VIEW>("prefilter_base", imageViews.front().handle);

    VkImageSubresourceRange subresourceRange;
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = 1;

    const auto levelCount = lightSource.prefiltered.levels;
    const auto levels = levelCount - 1;
    std::vector<VkImageSubresourceRange> subresources{};
    subresources.push_back(subresourceRange);

    for(auto i = 1; i < levelCount; ++i) {
        subresourceRange.baseMipLevel = i;
        subresourceRange.levelCount = levelCount - i;
        imageViews.push_back(lightSource.prefiltered.image.createView(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_VIEW_TYPE_2D, subresourceRange));
        device.setName<VK_OBJECT_TYPE_IMAGE_VIEW>(fmt::format("prefilter_{}", i), imageViews.back().handle);
        subresources.push_back(subresourceRange);
    }

    std::vector<VkDescriptorSet> srcSets;
    std::vector<VkDescriptorSet> dstSets;
    for(auto i = 0; i < (levelCount - 1); ++i) {
        auto sets = descriptorPool.allocate({ textureDescriptorSetLayout, imageDescriptorSetLayout });
        srcSets.push_back(sets.front());
        dstSets.push_back(sets.back());
    }

    std::vector<VkWriteDescriptorSet> writes(levels * 2, { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET });
    std::vector<VkDescriptorImageInfo> imageInfos(levels * 2);
    for(auto i = 0; i < levels; ++i) {
        writes[i].dstSet = srcSets[i];
        writes[i].dstBinding = 0;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].descriptorCount = 1;
        imageInfos[i] = { lightSource.prefiltered.sampler.handle, imageViews[i].handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        writes[i].pImageInfo = &imageInfos[i];

        writes[levels + i].dstSet = dstSets[i];
        writes[levels + i].dstBinding = 0;
        writes[levels + i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[levels + i].descriptorCount = 1;
        imageInfos[levels + i] = { VK_NULL_HANDLE, imageViews[i + 1].handle, VK_IMAGE_LAYOUT_GENERAL };
        writes[levels + i].pImageInfo = &imageInfos[levels + i];
    }

    device.updateDescriptorSets(writes);

    Synchronization synchronization{};
    synchronization._fence = device.createFence();
    synchronization._fence.reset();

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        std::array<VkDescriptorSet, 3> sets{};
        sets[2] = ltcDescriptorSet;

        auto consts = compute.prefilter.constants;
        const auto w = lightSource.texture.width;
        const auto h = lightSource.texture.height;
        consts.baseResolution = {w, h};
        for(auto i = 1; i < levelCount; ++i) {
            sets[0] = srcSets[i-1];
            sets[1] = dstSets[i-1];
            spdlog::info("prefiltering level: {}, width: {}, height: {}", i, (w >> i), (h >> i));
            consts.mipLevel = i;
            const auto gx = ((w >> i) + 7)/8;
            const auto gy = ((h >> i) + 7)/8;

            Barriers::push(lightSource.prefiltered.image, subresources[i], VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

            Barriers::flush(commandBuffer);

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.prefilter.pipeline.handle);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.prefilter.layout.handle, 0, COUNT(sets), sets.data(), 0, nullptr);
            vkCmdPushConstants(commandBuffer, compute.prefilter.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts), &consts);
            vkCmdDispatch(commandBuffer, gx, gy, 1);


            Barriers::push(lightSource.prefiltered.image, subresources[i], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            Barriers::flush(commandBuffer);

        }
        Barrier::computeWriteToFragmentRead(commandBuffer);

    }, synchronization);
    synchronization._fence.wait();
}


int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1024;
        settings.height = 1024;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.msaaSamples = VK_SAMPLE_COUNT_8_BIT;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = LinearlyTransformedCosines{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}