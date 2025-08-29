#include "VolumeRenderingIntro.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include <openvdb/openvdb.h>
#include <openvdb/io/Stream.h>

VolumeRenderingIntro::VolumeRenderingIntro(const Settings& settings) : VulkanBaseApp("Intro to Volume Rendering", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("intro_to_voume_rendering");
    fileManager().addSearchPathFront("intro_to_voume_rendering/data");
    fileManager().addSearchPathFront("intro_to_voume_rendering/spv");
    fileManager().addSearchPathFront("intro_to_voume_rendering/models");
    fileManager().addSearchPathFront("intro_to_voume_rendering/textures");
}

void VolumeRenderingIntro::initApp() {
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    loadPrimitives();
    initCamera();
    loadVolume();
    initUniforms();
    initOffscreen();
    loadBlueNoise();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    initTaa();
}

void VolumeRenderingIntro::initTaa() {
    jitter.sampler.type = static_cast<SamplerType>(SamplerType::Interleaved_Gradients);
    jitter.period(4);

    taa::Settings taa_settings{};
    taa_settings.resolution = { width, height};
    taa = std::make_unique<taa::Taa>(device, descriptorPool, bindlessDescriptor, gbuffer.color, gbuffer.depth, *camera, jitterValue, taa_settings);
    taa->init();
}

void VolumeRenderingIntro::initCamera() {
    OrbitingCameraSettings cameraSettings;
//    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.1;
    cameraSettings.orbitMaxZoom = 512.0f;
    cameraSettings.offsetDistance = 1.0f;
    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
//    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
//    camera->lookAt({0, 0, 100}, glm::vec3(0), {0, 1, 0});
}

void VolumeRenderingIntro::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void VolumeRenderingIntro::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);
}

void VolumeRenderingIntro::createDescriptorPool() {
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


void VolumeRenderingIntro::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void VolumeRenderingIntro::createDescriptorSetLayouts() {
    uniformDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("uniform_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
}

void VolumeRenderingIntro::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({uniformDescriptorSetLayout});
    uniformDescriptorSet = sets[0];

    auto writes = initializers::writeDescriptorSets<1>();

    writes[0].dstSet = uniformDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uniformInfo{ uniforms.gpu, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &uniformInfo;

    device.updateDescriptorSets(writes);
}

void VolumeRenderingIntro::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void VolumeRenderingIntro::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void VolumeRenderingIntro::createRenderPipeline() {
    //    @formatter:off
        render.pipeline =
            prototypes->cloneScreenSpaceGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("quad.vert.spv"))
                    .fragmentShader(resource("display.frag.spv"))
                .layout()
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                .name("render")
            .build(render.layout);

        render.procedural.pipeline =
            prototypes->cloneScreenSpaceGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("volume.vert.spv"))
                    .fragmentShader(resource("volume.frag.spv"))
                .colorBlendState()
                    .attachment().clear()
                        .enableBlend()
                        .colorBlendOp().add()
                        .alphaBlendOp().add()
                        .srcColorBlendFactor().one()
                        .dstColorBlendFactor().srcAlpha()
                        .srcAlphaBlendFactor().one()
                        .dstAlphaBlendFactor().one()
                    .add()
                .dynamicState()
                    .primitiveTopology()
                    .depthTestEnable()
                    .depthWriteEnable()
                .dynamicRenderPass()
                    .addColorAttachment(colorFormat)
                    .depthAttachment(VK_FORMAT_D16_UNORM)
                .layout()
                    .addPushConstantRange(Camera::pushConstant())
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                    .addDescriptorSetLayout(uniformDescriptorSetLayout)
                .name("procedural_volume")
            .build(render.procedural.layout);

        render.grid.pipeline =
            prototypes->cloneScreenSpaceGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("volume.vert.spv"))
                    .fragmentShader(resource("volume_grid.frag.spv"))
                .colorBlendState()
                    .attachment().clear()
                        .enableBlend()
                        .colorBlendOp().add()
                        .alphaBlendOp().add()
                        .srcColorBlendFactor().one()
                        .dstColorBlendFactor().srcAlpha()
                        .srcAlphaBlendFactor().one()
                        .dstAlphaBlendFactor().one()
                    .add()
                .dynamicState()
                    .primitiveTopology()
                    .depthTestEnable()
                    .depthWriteEnable()
                .dynamicRenderPass()
                    .addColorAttachment(colorFormat)
                    .depthAttachment(VK_FORMAT_D16_UNORM)
                .layout()
                    .addPushConstantRange(Camera::pushConstant())
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                    .addDescriptorSetLayout(uniformDescriptorSetLayout)
                .name("grid_volume")
            .build(render.grid.layout);
    //    @formatter:on
}


void VolumeRenderingIntro::onSwapChainDispose() {
    dispose(render.pipeline);
}

void VolumeRenderingIntro::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

void VolumeRenderingIntro::newFrame() {
    if(taaEnabled) {
        jitterValue = (-1.f + 2.f * jitter.nextSample());
        jitterValue /= glm::vec2(width, height);

        camera->newFrame();
        camera->jitter(jitterValue.x, jitterValue.y);
        taa->newFrame();
    } else {
        jitterValue = glm::vec2(0);
    }
}

VkCommandBuffer *VolumeRenderingIntro::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    clearColor(0, 0, 0);

    renderOffscreen(commandBuffer);

    renderToSwapChain([&]{
        renderScene(commandBuffer);
        renderUI(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void VolumeRenderingIntro::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Settings");
    ImGui::SetWindowSize({0, 0});
    ImGui::SliderInt("density", &uniforms.cpu->density_method, 0, 5);
    ImGui::SliderFloat("frequency", &uniforms.cpu->frequency, 0.5, 16);
    ImGui::SliderFloat("bias", &uniforms.cpu->bias, 0, 0.9);
    ImGui::SliderFloat("falloff", &uniforms.cpu->falloff, 0, 1);
    ImGui::SliderFloat("scattering", &uniforms.cpu->scatter.w, 0, 100);
    ImGui::ColorEdit3("scattering color.", &uniforms.cpu->scatter.x);
    ImGui::SliderFloat("absorption", &uniforms.cpu->absorption.w, 0, 100);
    ImGui::ColorEdit3("absorption color.", &uniforms.cpu->absorption.x);
    ImGui::SliderFloat("emission zero", &uniforms.cpu->emission_zero, 0, volume.maxEmission);
    ImGui::SliderFloat("scale", &scale, 0.1, 100);
    ImGui::Checkbox("TAA", &taaEnabled);
    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void VolumeRenderingIntro::renderOffscreen(VkCommandBuffer commandBuffer) {
    renderVolume(commandBuffer);
    if(taaEnabled) {
        taa->exec(commandBuffer);
    }
}

void VolumeRenderingIntro::renderVolume(VkCommandBuffer commandBuffer) {
    Barrier::fragmentReadToFragmentWrite(commandBuffer);
    offscreen.render(commandBuffer, renderInfo, [&] {
        static std::array<VkDescriptorSet, 2> sets;
        sets[0] = bindlessDescriptor.descriptorSet;
        sets[1] = uniformDescriptorSet;

        const auto& pipeline = uniforms.cpu->density_method != 5 ? render.procedural : render.grid;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);

        camera->push(commandBuffer, pipeline.layout);
        VkDeviceSize offset = 0;

        vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
        vkCmdSetDepthTestEnable(commandBuffer, VK_TRUE);
        vkCmdSetDepthWriteEnable(commandBuffer, VK_TRUE);

        AppContext::renderClipSpaceQuad(commandBuffer);
    });
    Barrier::fragmentWriteToFragmentRead(commandBuffer);

}

void VolumeRenderingIntro::renderScene(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 1> sets;
    sets[0] = bindlessDescriptor.descriptorSet;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void VolumeRenderingIntro::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }

    setTitle(fmt::format("{}, FPS - {}", title, framePerSecond));
}

void VolumeRenderingIntro::checkAppInputs() {
    camera->processInput();
}

void VolumeRenderingIntro::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void VolumeRenderingIntro::onPause() {
    VulkanBaseApp::onPause();
}

void VolumeRenderingIntro::initOffscreen() {
    textures::create(device, gbuffer.color, VK_IMAGE_TYPE_2D, colorFormat, {width, height, 1});
    textures::create(device, gbuffer.depth, VK_IMAGE_TYPE_2D, VK_FORMAT_D16_UNORM, {width, height, 1});

    renderInfo = Offscreen::RenderInfo{
        .colorAttachments = { { gbuffer.color.imageView, colorFormat, {0.572, 0.772, 0.921, 1} } },
        .depthAttachment = {{ gbuffer.depth.imageView, VK_FORMAT_D16_UNORM }},
        .renderArea = { width, height}
    };
    bindlessDescriptor.update({&gbuffer.color, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, uniforms.cpu->color_tex_id});
    uniforms.cpu->depth_tex_id = bindlessDescriptor.update(gbuffer.depth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

}

void VolumeRenderingIntro::initUniforms() {
    UniformData defaults{};
    uniforms.gpu = device.createCpuVisibleBuffer(&defaults, sizeof(UniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    uniforms.cpu = reinterpret_cast<UniformData*>(uniforms.gpu.map());
    device.setName<VK_OBJECT_TYPE_BUFFER>("uniforms", uniforms.gpu.buffer);

    uniforms.cpu->near = camera->near();
    uniforms.cpu->far = camera->far();
    uniforms.cpu->resolution = {width, height};

    auto center = (volume.bounds.min + volume.bounds.max) * 0.5f;
    auto moveToOrigin = glm::translate(glm::mat4{1}, -center);
    auto textureToWorldSpace = glm::scale(glm::mat4{1}, glm::vec3(0.1)) * moveToOrigin * volume.localToWorld;

    uniforms.cpu->bmin = (textureToWorldSpace * glm::vec4(0, 0, 0, 1));
    uniforms.cpu->bmax = (textureToWorldSpace * glm::vec4(1, 1, 1, 1));
    uniforms.cpu->volume_tex_id = volume.binding_id;
    uniforms.cpu->volume_emission_tex_id = volume.emission_binding_id;
    uniforms.cpu->max_density = volume.maxDensity;
    uniforms.cpu->max_emission = volume.maxEmission;
    uniforms.cpu->worldToTextureSpace = glm::inverse(textureToWorldSpace);
}

void VolumeRenderingIntro::loadPrimitives() {
    auto prim = primitives::sphere(100, 100, 1.0f, glm::mat4{1}, glm::vec4(1, 0, 0, 1), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    primitives.sphere.vertices = device.createDeviceLocalBuffer(prim.vertices.data(), BYTE_SIZE(prim.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    primitives.sphere.indexes = device.createDeviceLocalBuffer(prim.indices.data(), BYTE_SIZE(prim.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void VolumeRenderingIntro::endFrame() {
    static bool once = true;
    auto& cam = camera->cam();
    uniforms.cpu->projection = cam.proj;
    uniforms.cpu->view = cam.view;
    uniforms.cpu->frame++;
    taa->endFrame();

    auto center = (volume.bounds.min + volume.bounds.max) * 0.5f;
    auto moveToOrigin = glm::translate(glm::mat4{1}, -center);
    auto textureToWorldSpace = glm::scale(glm::mat4{1}, glm::vec3(scale/100)) * moveToOrigin * volume.localToWorld;
    uniforms.cpu->worldToTextureSpace = glm::inverse(textureToWorldSpace);
}

void VolumeRenderingIntro::loadBlueNoise() {
    std::vector<std::string> paths;
    for(auto i = 0; i < 64; ++i) {
        paths.push_back(resource(std::format("fast_noise/128_128/uniform/R_{}.png", i)));
    }
    textures::fromFile(device, blueNoise, paths);
    uniforms.cpu->blue_noise_tex_id = bindlessDescriptor.update(blueNoise, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
}

glm::mat4 extractIndexToWorldMatrix(openvdb::GridBase::ConstPtr grid)
{
    // Ensure grid has a linear transform (AffineMap)
    const openvdb::math::Transform& transform = grid->transform();
    const auto mat = dynamic_cast<const openvdb::math::ScaleTranslateMap*>(transform.baseMap().get());

    if(mat) {
        const auto &s = mat->getScale();
        auto scale = glm::scale(glm::mat4{1}, glm::vec3{s.x(), s.y(), s.z()});

        const auto &t = mat->getTranslation();
        auto translate = glm::translate(glm::mat4{1}, glm::vec3{t.x(), t.y(), t.z()});

        return translate * scale;

    }

    return glm::mat4{1};
}

void loadVdbVolume(VulkanDevice& device, openvdb::io::File& vdbFile, const std::string name, Texture& texture, glm::mat4& worldToTextureSpace, float& maxValue) {
    auto grid = openvdb::gridPtrCast<openvdb::FloatGrid>(vdbFile.readGrid(name));
    auto numVoxels = grid->activeVoxelCount();
    auto b = grid->evalActiveVoxelBoundingBox();
    auto bmin = grid->indexToWorld(b.min());
    auto bmax = grid->indexToWorld(b.max());
    auto vs = grid->voxelSize();
    auto dim = grid->evalActiveVoxelDim();

    std::stringstream ss;

    ss << fmt::format("\nvolume: {}\n", grid->getName());
    ss << fmt::format("\nvoxel size: [{}, {}, {}]\n", vs.x(), vs.y(), vs.z());
    ss << fmt::format("\tvoxel count: {}\n", numVoxels);
    ss << fmt::format("\tindex bounds: [[{}, {}, {}], [{}, {}, {}]]\n"
            , b.min().x(), b.min().y(), b.min().z()
            , b.max().x(), b.max().y(), b.max().z());
    ss << fmt::format("\tworld bounds: [[{}, {}, {}], [{}, {}, {}]]\n"
            , bmin.x(), bmin.y(), bmin.z()
            , bmax.x(), bmax.y(), bmax.z());
    ss << fmt::format("\tdimension: [{}, {}, {}]\n", dim.x(), dim.y(), dim.z());
    ss << fmt::format("\tdimension: [{}, {}, {}]\n", b.max().x() - b.min().x(), b.max().y() - b.min().y(), b.max().z() - b.min().z());

    spdlog::info(ss.str());

    VulkanBuffer stagingBuffer = device.createStagingBuffer(dim.x() * dim.y() * dim.z() * sizeof(float));
    auto voxels = stagingBuffer.span<float>();
    std::fill(voxels.begin(), voxels.end(), grid->background());

    maxValue = std::numeric_limits<float>::lowest();
    auto v = grid->beginValueOn();
    for (; v.next() ;) {
        openvdb::Coord p = v.getCoord() - b.min();

        int x = p.x();
        int y = p.y();
        int z = p.z();

        int index = (z * dim.y() + y) * dim.x() + x;

        if (x < 0 || x >= dim.x() ||
            y < 0 || y >= dim.y() ||
            z < 0 || z >= dim.z()) {
            std::cerr << "Out of bounds: " << v.getCoord() << std::endl;
            continue;
        }

        voxels[index] = *v;
        maxValue = std::max(*v, maxValue);
    }

    glm::vec3 wmin{bmin.x(), bmin.y(), bmin.z()};
    glm::vec3 wmax{bmax.x(), bmax.y(), bmax.z()};

    glm::vec3 translate = -wmin;
    glm::vec3 scale = 1.f/(wmax - wmin);
    worldToTextureSpace = glm::scale(glm::mat4(1.0f), scale) * glm::translate(glm::mat4(1.0f), translate);


    textures::createNoTransition(device, texture, VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT, {dim.x(), dim.y(), dim.z()});

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        Barriers::pushAndFlush(commandBuffer, texture.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE
                , VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_NONE, VK_ACCESS_TRANSFER_WRITE_BIT
                , VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy2 region{ VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2 };
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {to<uint32_t>(dim.x()), to<uint32_t>(dim.y()), to<uint32_t>(dim.z()) };

        VkCopyBufferToImageInfo2 copyInfo{
                .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2,
                .srcBuffer = stagingBuffer.buffer,
                .dstImage = texture.image,
                .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .regionCount = 1,
                .pRegions = &region
        };
        vkCmdCopyBufferToImage2(commandBuffer, &copyInfo);

        Barriers::pushAndFlush(commandBuffer, texture.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_TRANSFER_BIT
                , VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT
                , VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });

}

void VolumeRenderingIntro::loadVolume() {
    openvdb::initialize();
    openvdb::io::File file(resource("bunny_cloud.vdb"));

    assert(file.open());

    loadVdbVolume(device, file, "density", volume.density, volume.worldToLocal, volume.maxDensity);

    volume.maxDensity += std::numeric_limits<float>::epsilon();
    volume.localToWorld = glm::inverse(volume.worldToLocal);
    volume.bounds.min = (volume.localToWorld * glm::vec4(0, 0, 0, 1)).xyz;
    volume.bounds.max = (volume.localToWorld * glm::vec4(1)).xyz;
    volume.binding_id = bindlessDescriptor.update(volume.density, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    for(auto nameItr = file.beginName(); nameItr != file.endName(); ++nameItr) {
        auto name = nameItr.gridName();
        if(name == "temperature") {
            glm::mat4 worldToTextureSpace;
            loadVdbVolume(device, file, "temperature", volume.emission, worldToTextureSpace, volume.maxEmission);
            volume.emission_binding_id = bindlessDescriptor.update(volume.emission, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        }
    }
}


int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1280;
        settings.height = 720;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = VolumeRenderingIntro{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}