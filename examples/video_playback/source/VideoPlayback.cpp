#include "VideoPlayback.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "ExtensionChain.hpp"
#include "video/h264.hpp"
#include "video/minimp4.hpp"
#include "Barrier.hpp"
#include "AppContext.hpp"
#include "video/VideoParser.hpp"

VideoPlayback::VideoPlayback(const Settings &settings) : VulkanBaseApp("Video playback", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/video");
    fileManager().addSearchPathFront("video_playback");
    fileManager().addSearchPathFront("video_playback/spv");
    fileManager().addSearchPathFront("video_playback/models");
    fileManager().addSearchPathFront("video_playback/textures");
}

void VideoPlayback::initApp() {
    initVideoDecoder();
    loadVideo();
    initVideoInstance();
    initCamera();
    createDescriptorPool();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
}


void VideoPlayback::initCamera() {
    OrbitingCameraSettings cameraSettings;
//    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.1;
    cameraSettings.orbitMaxZoom = 512.0f;
    cameraSettings.offsetDistance = 1.0f;
    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width) / float(swapChain.extent.height);

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager &>(*this), cameraSettings);
}


void VideoPlayback::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 4> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets},
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT);
}

void VideoPlayback::createDescriptorSetLayouts() {
    displayDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("display_video")
            .bindless()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
                .immutableSamplers(decoder->getSampler())
        .createLayout();
    auto sets = descriptorPool.allocate( { displayDescriptorSetLayout } );
    displayDescriptorSet = sets[0];
}

void VideoPlayback::updateDescriptorSets() {
//    updateDescriptorBinding(display.texture);
}

void VideoPlayback::updateDescriptorBinding(const Texture &texture) {

    static VkDescriptorImageInfo imageInfo{ VK_NULL_HANDLE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    static auto writes = initializers::writeDescriptorSets<1>();
    writes[0].dstSet = displayDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &imageInfo;

    imageInfo.imageView = texture.imageView.handle;
    device.updateDescriptorSets(writes);
}

void VideoPlayback::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics,
                                           VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void VideoPlayback::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void VideoPlayback::createRenderPipeline() {
    //    @formatter:off
        render.pipeline =
            prototypes->cloneScreenSpaceGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("quad.vert.spv"))
                    .fragmentShader(resource("video.frag.spv"))
                .layout()
                    .addDescriptorSetLayout(displayDescriptorSetLayout)
                .name("video_player")
                .build(render.layout);
    //    @formatter:on
}


void VideoPlayback::onSwapChainDispose() {
    dispose(render.pipeline);
}

void VideoPlayback::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *VideoPlayback::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto &commandBuffer = commandBuffers[imageIndex];

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

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, 1, &displayDescriptorSet, 0, nullptr);
    AppContext::renderClipSpaceQuad(commandBuffer);

    renderControls(commandBuffer);
    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void VideoPlayback::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
    video_instance->update(time);
    if(video_instance->output.display.texture.isValid()) {
        updateDescriptorBinding(video_instance->output.display.texture);
    }
    setTitle(fmt::format("{}, FPS: {}", title, framePerSecond));
}

void VideoPlayback::checkAppInputs() {
    camera->processInput();
}

void VideoPlayback::cleanup() {
    AppContext::shutdown();
}

void VideoPlayback::onPause() {
    VulkanBaseApp::onPause();
}

void VideoPlayback::beforeDeviceCreation() {
    auto devFeatures11 = findExtension<VkPhysicalDeviceVulkan11Features>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, deviceCreateNextChain);
    if (devFeatures11.has_value()) {
        devFeatures11.value()->samplerYcbcrConversion = VK_TRUE;
    } else {
        static VkPhysicalDeviceVulkan11Features devFeatures11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
        devFeatures11.samplerYcbcrConversion = VK_TRUE;
    }


    auto devFeatures12 = findExtension<VkPhysicalDeviceVulkan12Features>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    if (devFeatures12.has_value()) {
        devFeatures12.value()->scalarBlockLayout = VK_TRUE;
        devFeatures12.value()->shaderOutputViewportIndex = VK_TRUE;
    } else {
        static VkPhysicalDeviceVulkan12Features devFeatures12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        devFeatures12.scalarBlockLayout = VK_TRUE;
        devFeatures12.shaderOutputViewportIndex = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, devFeatures12);
    }

    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    if (devFeatures13.has_value()) {
        devFeatures13.value()->synchronization2 = VK_TRUE;
        devFeatures13.value()->dynamicRendering = VK_TRUE;
        devFeatures13.value()->maintenance4 = VK_TRUE;
    } else {
        static VkPhysicalDeviceVulkan13Features devFeatures13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        devFeatures13.synchronization2 = VK_TRUE;
        devFeatures13.dynamicRendering = VK_TRUE;
        devFeatures13.maintenance4 = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, devFeatures13);
    }

    static VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dsFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };
    dsFeatures.extendedDynamicState3PolygonMode = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, dsFeatures);
}

void VideoPlayback::loadVideo() {
    auto parser = video::VideoParser{device};
    video = parser.parse(resource("855289-hd_1920_1080_25fps.mp4"));
    
    std::vector<int> intra_frames{};
    for(auto i = 0; i < video->slice_header_count; ++i) {
        auto& header = video->slice_header_datas[i];
        if(header.slice_type == 7) {
            intra_frames.push_back(i);
        }
    }

    std::vector<int> display_order;
    for(auto i = 0; i < 20; ++i) {
        display_order.push_back(video->frame_infos[i].display_order);
    }
}



void VideoPlayback::initVideoInstance() {
    video_instance = std::make_shared<VideoInstance>(video);
    video_instance->current_decode_frame = 0;
    video_instance->flags &= ~VideoInstance::Flags::FirstFrameDecoded;
    video_instance->flags |= VideoInstance::Flags::Playing | VideoInstance::Flags::Looped;
    video_instance->output_textures_free.clear();
    video_instance->output_textures_used.clear();
    video_instance->output_textures_resolve_request.clear();
    video_instance->maxFrames = 10;
}


void VideoPlayback::endFrame() {
    decoder->decode(video_instance);
}


void VideoPlayback::renderControls(VkCommandBuffer commandBuffer) {

    static float pos = 0;
    static bool playing = false;
    static bool loop = false;

    playing = has_flag(video_instance->flags, VideoInstance::Flags::Playing);
    loop = has_flag(video_instance->flags, VideoInstance::Flags::Looped);
    pos = video_instance->current_time;
    ImGui::Begin("video controls");
    ImGui::SetWindowSize({350, 60});

    auto action = playing ? "Stop" : "Play";
    if(ImGui::Button(action)) {
        if(playing) {
            video_instance->flags &= ~VideoInstance::Flags::Playing;
        }else {
            video_instance->flags |= VideoInstance::Flags::Playing;
        }
    }
    ImGui::SameLine();
    auto updated = ImGui::SliderFloat("", &pos, 0, video_instance->video->duration_seconds);
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &loop);

    if(loop) {
        video_instance->flags |= VideoInstance::Flags::Looped;
    }else {
        video_instance->flags &= ~VideoInstance::Flags::Looped;
    }

    ImGui::End();

    if(updated) {
        video_instance->seek(pos);
    }
    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void VideoPlayback::initVideoDecoder() {
    decoder = std::make_unique<VideoDecoder>(device);
    decoder->init();
}

int main() {
    try {
        fs::current_path("../../../../examples");
        Settings settings;
        settings.uniqueQueueFlags |= VK_QUEUE_VIDEO_DECODE_BIT_KHR;
        settings.depthTest = true;
        settings.enableBindlessDescriptors = true;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME);
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();

        auto app = VideoPlayback{settings};
        app.addPlugin(imGui);
        app.run();
    } catch (std::runtime_error &err) {
        spdlog::error(err.what());
    }
}