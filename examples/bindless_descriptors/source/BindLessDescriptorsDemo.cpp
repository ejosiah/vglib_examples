#include "BindLessDescriptorsDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include <plugins/BindLessDescriptorPlugin.hpp>

BindLessDescriptorsDemo::BindLessDescriptorsDemo(const Settings& settings) : VulkanBaseApp("Bindless descriptors", settings) {
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/bindless_descriptors");
    fileManager.addSearchPathFront("../../examples/bindless_descriptors/data");
    fileManager.addSearchPathFront("../../examples/bindless_descriptors/spv");
    fileManager.addSearchPathFront("../../examples/bindless_descriptors/models");
    fileManager.addSearchPathFront("../../examples/bindless_descriptors/textures");

    static VkPhysicalDeviceSynchronization2Features sync2Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
    sync2Features.synchronization2 = VK_TRUE;

    deviceCreateNextChain = &sync2Features;
}

void BindLessDescriptorsDemo::initApp() {
    initGlobals();
    loader = std::make_unique<asyncml::Loader>( &device, 32);
    loader->start();
    createDescriptorPool();
    loadModel();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    initCamera();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
}

void BindLessDescriptorsDemo::loadModel() {
    _sponza = loader->load(R"(C:/Users/Josiah Ebhomenye/source/repos/VolumetricLighting/bin/Debug/meshes/sponza.obj)", centimetre);
//    _sponza = loader->load(R"(C:/Users/Josiah Ebhomenye/OneDrive/media/models/amazon_lumberyard_bistro/Exterior/exterior.obj)", centimetre);
//    _sponza = loader->load(R"(C:/Users/Josiah Ebhomenye/OneDrive/media/models/amazon_lumberyard_bistro/Interior/interior.obj)", centimetre);
//    _sponza = loader->load(R"(C:/Users/Josiah Ebhomenye/OneDrive/media/models/sibenik/sibenik.obj)");
//    phong::load(R"(C:/Users/Josiah Ebhomenye/source/repos/VolumetricLighting/bin/Debug/meshes/sponza.obj)", device, descriptorPool, sponza, {}, false, 1, centimetre);
//    using namespace std::chrono_literals;
//    std::this_thread::sleep_for(5s);
}

void BindLessDescriptorsDemo::initCamera() {
//    OrbitingCameraSettings cameraSettings;
    FirstPersonSpectatorCameraSettings cameraSettings;
//    cameraSettings.orbitMinZoom = 0.1;
//    cameraSettings.orbitMaxZoom = 512.0f;
//    cameraSettings.offsetDistance = 1.0f;
//    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);
    cameraSettings.acceleration = glm::vec3(1 * m);
    cameraSettings.velocity = glm::vec3(5 * m);
    cameraSettings.zFar = 200 * m;
    cameraSettings.zNear = 1 * cm;

//    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
//    auto position = (sponza.bounds.max + sponza.bounds.min) * 0.5f;
    auto position = glm::vec3(-0.6051893234, 6.5149531364, -0.3869051933);
    spdlog::info("camera position: {}", position);
    camera->position(position);
}

void BindLessDescriptorsDemo::initGlobals() {
    textures::color(device, dummyTexture, glm::vec3{0.2}, glm::uvec3{64});
}

void BindLessDescriptorsDemo::createDescriptorPool() {
    constexpr uint32_t maxSets = 400;
    std::array<VkDescriptorPoolSize, 15> poolSizes{
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
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void BindLessDescriptorsDemo::createDescriptorSetLayouts() {
    auto bindings =
        device.descriptorSetLayoutBuilder()
            .name("mesh_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(_sponza->numMeshes())
                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(_sponza->numMaterials())
                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .build();

    plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).addBindings(bindings);
    plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).createDescriptorSetLayout();
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet(1);
}

void BindLessDescriptorsDemo::updateDescriptorSets(){

    auto writes = initializers::writeDescriptorSets();
    writes.resize(BindLessDescriptorPlugin::MaxDescriptorResources);

    VkDescriptorImageInfo dummyImageInfo{ dummyTexture.sampler.handle, dummyTexture.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    for(auto i = 0; i < writes.size(); i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = bindlessDescriptor.descriptorSet;
        writes[i].dstBinding = BindLessDescriptorPlugin::TextureResourceBindingPoint;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].dstArrayElement = i;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &dummyImageInfo;
    }

    device.updateDescriptorSets(writes);

    const auto numMeshes = _sponza->numMeshes();
    const VkDeviceSize meshDataSize = sizeof(asyncml::MeshData);
    std::vector<VkDescriptorBufferInfo> meshInfos;
    for(auto i = 0; i < numMeshes; i++) {
        const VkDeviceSize offset = i * meshDataSize;
        meshInfos.push_back({_sponza->meshBuffer, offset, meshDataSize  });
    }

    writes.resize(2);
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = bindlessDescriptor.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].dstArrayElement = 0;
    writes[0].descriptorCount = numMeshes;
    writes[0].pBufferInfo = meshInfos.data();

    const auto numMaterials = _sponza->numMaterials();
    const VkDeviceSize materialDataSize = sizeof(asyncml::MaterialData);
    std::vector<VkDescriptorBufferInfo> materialInfos;
    for(auto i = 0; i < numMaterials; i++){
        materialInfos.push_back({_sponza->materialBuffer, i * materialDataSize, materialDataSize});
    }
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = bindlessDescriptor.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].dstArrayElement = 0;
    writes[1].descriptorCount = numMaterials;
    writes[1].pBufferInfo = materialInfos.data();

    device.updateDescriptorSets(writes);
}

void BindLessDescriptorsDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void BindLessDescriptorsDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void BindLessDescriptorsDemo::createRenderPipeline() {
    //    @formatter:off
        auto bindlessDescriptorSetLayout = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSetLayout();
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("render.vert.spv"))
                    .fragmentShader(resource("sponza.frag.spv"))
                .layout()
                    .addDescriptorSetLayout(bindlessDescriptorSetLayout)
                .name("render")
                .build(render.layout);
    //    @formatter:on
}


void BindLessDescriptorsDemo::onSwapChainDispose() {
    dispose(render.pipeline);
}

void BindLessDescriptorsDemo::onSwapChainRecreation() {
    camera->perspective(swapChain.aspectRatio());
    createRenderPipeline();
}

VkCommandBuffer *BindLessDescriptorsDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
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

    static std::array<VkDescriptorSet, 1> sets{};
    sets[0] = bindlessDescriptor.descriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, sets.size(), sets.data(), 0, nullptr);
    camera->push(commandBuffer, render.layout);
//    sponza.draw(commandBuffer);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, _sponza->vertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, _sponza->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexedIndirect(commandBuffer, _sponza->draw.gpu, 0, _sponza->draw.count, sizeof(VkDrawIndexedIndirectCommand));

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void BindLessDescriptorsDemo::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
}

void BindLessDescriptorsDemo::checkAppInputs() {
    camera->processInput();
}

void BindLessDescriptorsDemo::cleanup() {
    loader->stop();
}

void BindLessDescriptorsDemo::onPause() {
    VulkanBaseApp::onPause();
}

void BindLessDescriptorsDemo::endFrame() {
    _sponza->updateDrawState(device, bindlessDescriptor);

}


int main(){
    try{

        Settings settings;
        settings.depthTest = true;
        settings.uniqueQueueFlags |= VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        auto app = BindLessDescriptorsDemo{ settings };
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}