#include "GltfViewer.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include "L2DFileDialog.h"

#include <cstring>

GltfViewer::GltfViewer(const Settings& settings) : VulkanBaseApp("Gltf Viewer", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../../examples/gltf_viewer");
    fileManager().addSearchPathFront("../../examples/gltf_viewer/data");
    fileManager().addSearchPathFront("../../examples/gltf_viewer/spv");
    fileManager().addSearchPathFront("../../examples/gltf_viewer/models");
    fileManager().addSearchPathFront("../../examples/gltf_viewer/textures");
    fileManager().addSearchPathFront("../../dependencies/glTF-Sample-Assets");
    fileManager().addSearchPathFront("../../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../../examples/common/data");
}

void GltfViewer::initApp() {
    constructModelPaths();
    initBindlessDescriptor();
    initScreenQuad();
    initCamera();
    initUniforms();
    createDescriptorPool();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    createGBuffer();
    createFrameBufferTexture();
    initLoader();
    createConstantTextures();
    createSkyBox();
    createDescriptorSetLayouts();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
    updateDescriptorSets();
    createConvolutionSampler();
    loadTextures();
    initModels();
}

void GltfViewer::initScreenQuad() {
    auto vertices = ClipSpace::Quad::positions;
    screenQuad  = device.createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void GltfViewer::initCamera() {
    OrbitingCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.1;
    cameraSettings.orbitMaxZoom = 512.0f;
    cameraSettings.offsetDistance = 1.0f;
    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera->zoomDelta = 0.05;
}

void GltfViewer::initUniforms() {
    UniformData defaults{};
    gBuffer.uniforms.resize(swapChainImageCount);
    for(auto i = 0; i < swapChainImageCount; ++i) {
        gBuffer.uniforms[i] = device.createDeviceLocalBuffer(&defaults, sizeof(UniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT );
    }
    
    transmissionFramebuffer.uniforms.resize(swapChainImageCount);
    for(auto i = 0; i < swapChainImageCount; ++i) {
        transmissionFramebuffer.uniforms[i] = device.createDeviceLocalBuffer(&defaults, sizeof(UniformData),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }
}

void GltfViewer::createGBuffer() {
    gBuffer.color.resize(swapChainImageCount);
    gBuffer.depth.resize(swapChainImageCount);
    gBuffer.UniformsDescriptorSet.resize(swapChainImageCount);
    gBuffer.info.resize(swapChainImageCount);
    const auto size = gBuffer.color.size();
    for(auto i = 0; i < size; ++i) {
        gBuffer.color[i].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        gBuffer.color[i].bindingId = i + TextureConstants::COUNT;

        textures::create(device, gBuffer.color[i], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {swapChain.width(), swapChain.height(), 1});
        bindlessDescriptor.update({&gBuffer.color[i], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  gBuffer.color[i].bindingId});

        textures::create(device, gBuffer.depth[i], VK_IMAGE_TYPE_2D, VK_FORMAT_D16_UNORM, {swapChain.width(), swapChain.height(), 1});

        gBuffer.info[i] = Offscreen::RenderInfo{
                .colorAttachments = {{&gBuffer.color[i], VK_FORMAT_R32G32B32A32_SFLOAT}},
                .depthAttachment = {{&gBuffer.depth[i], VK_FORMAT_D16_UNORM}},
                .renderArea = {gBuffer.color[i].width, gBuffer.color[i].height}
        };
    }
}

void GltfViewer::createFrameBufferTexture() {
    transmissionFramebuffer.color.resize(swapChainImageCount);
    transmissionFramebuffer.depth.resize(swapChainImageCount);
    transmissionFramebuffer.UniformsDescriptorSet.resize(swapChainImageCount);

    transmissionFramebuffer.info.resize(swapChainImageCount);
        const auto size = transmissionFramebuffer.color.size();
        for(auto i = 0; i < size; ++i) {
            transmissionFramebuffer.color[i].levels = static_cast<uint32_t>(std::log2(1024)) + 1 - lowestLod;
            transmissionFramebuffer.color[i].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            transmissionFramebuffer.color[i].bindingId = gBuffer.color.back().bindingId + i + 1;

            textures::create(device, transmissionFramebuffer.color[i], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {swapChain.width(), swapChain.height(), 1});
            bindlessDescriptor.update({&transmissionFramebuffer.color[i], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  transmissionFramebuffer.color[i].bindingId});

            textures::create(device, transmissionFramebuffer.depth[i], VK_IMAGE_TYPE_2D, VK_FORMAT_D16_UNORM, {swapChain.width(), swapChain.height(), 1});

            transmissionFramebuffer.info[i] = Offscreen::RenderInfo{
                    .colorAttachments = {{&transmissionFramebuffer.color[i], VK_FORMAT_R32G32B32A32_SFLOAT}},
                    .depthAttachment = {{&transmissionFramebuffer.depth[i], VK_FORMAT_D16_UNORM}},
                    .renderArea = {transmissionFramebuffer.color[i].width, transmissionFramebuffer.color[i].height}
            };
        }
}

void GltfViewer::initBindlessDescriptor() {
    bindingOffset += to<int>(swapChainImageCount) * 2;
    int reservation = to<int>(environmentPaths.size()) * textureSetWidth + bindingOffset;
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, reservation);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void GltfViewer::createConstantTextures() {
    textures::color(device, constantTextures[TextureConstants::BLACK], glm::vec3(0), {32, 32});
    textures::color(device, constantTextures[TextureConstants::WHITE], glm::vec3(1), {32, 32});
    textures::normalMap(device, constantTextures[TextureConstants::NORMAL], {32, 32});

    constantTextures[TextureConstants::BLACK].bindingId = TextureConstants::BLACK;
    constantTextures[TextureConstants::WHITE].bindingId = TextureConstants::WHITE;
    constantTextures[TextureConstants::NORMAL].bindingId = TextureConstants::NORMAL;

    bindlessDescriptor.update(constantTextures[TextureConstants::BLACK], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindlessDescriptor.update(constantTextures[TextureConstants::WHITE], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindlessDescriptor.update(constantTextures[TextureConstants::NORMAL], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    constantTextures[TextureConstants::BRDF_LUT].bindingId = TextureConstants::BRDF_LUT;
    loader->loadTexture(resource("lut/lut_ggx.png"), constantTextures[TextureConstants::BRDF_LUT]);

    constantTextures[TextureConstants::SHEEN_LUT].bindingId = TextureConstants::SHEEN_LUT;
    loader->loadTexture(resource("lut/lut_sheen_E.png"), constantTextures[TextureConstants::SHEEN_LUT]);

    constantTextures[TextureConstants::CHARLIE_LUT].bindingId = TextureConstants::CHARLIE_LUT;
    loader->loadTexture(resource("lut/lut_charlie.png"), constantTextures[TextureConstants::CHARLIE_LUT]);

    uniforms.brdf_lut_texture_id = constantTextures[TextureConstants::BRDF_LUT].bindingId;
    uniforms.sheen_lut_texture_id = constantTextures[TextureConstants::SHEEN_LUT].bindingId;
    uniforms.charlie_lut_texture_id = constantTextures[TextureConstants::CHARLIE_LUT].bindingId;
}

void GltfViewer::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    if(devFeatures13.has_value()) {
        devFeatures13.value()->synchronization2 = VK_TRUE;
        devFeatures13.value()->dynamicRendering = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan13Features devFeatures13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        devFeatures13.synchronization2 = VK_TRUE;
        devFeatures13.dynamicRendering = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, devFeatures13);
    };

    static VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dsFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };
    dsFeatures.extendedDynamicState3PolygonMode = VK_TRUE;
    dsFeatures.extendedDynamicState3ColorBlendEnable = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, dsFeatures);

    static VkPhysicalDeviceIndexTypeUint8FeaturesEXT indexType8{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT };
    indexType8.indexTypeUint8 = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, indexType8);
}

void GltfViewer::createDescriptorPool() {
    constexpr uint32_t maxSets = 1000;
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


void GltfViewer::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void GltfViewer::createConvolutionSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    convolution.sampler = device.createSampler(samplerInfo);
}

void GltfViewer::loadTextures() {
    environments.resize(environmentPaths.size());
    stagingTextures.resize(environmentPaths.size());

    std::vector<std::shared_ptr<gltf::TextureUploadStatus>> uploadStatuses;
    for(auto i = 0; i < environments.size(); ++i) {
        environments[i].bindingId = i + bindingOffset;

        stagingTextures[i].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        stagingTextures[i].flipped = true;

        auto status = loader->loadTexture(fmt::format("{}/{}", resource("environments"), environmentPaths[i]), stagingTextures[i]);
        uploadStatuses.push_back(std::move(status));
    }


    runInBackground([this, uploadStatuses]{
        for(auto i = 0; i < uploadStatuses.size(); ++i) {
            auto& status = uploadStatuses[i];
            status->sync();

            const auto width = 2048u;
//            environments[i].levels = to<uint32_t>(std::log2(width) + 1) - lowestLod;
//            environments[i].lod = true;
            textures::createNoTransition(device, environments[i], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, width, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
            convertToOctahedralMap(*status->texture, environments[i]);
        }

        for(auto i = 0; i < environments.size(); ++i) {
            Texture irradianceTexture;
            irradianceTexture.bindingId = environments.size() + i + bindingOffset;
            textures::createNoTransition(device, irradianceTexture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {512, 512, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
            createIrradianceMap(environments[i], irradianceTexture);
            irradianceMaps.push_back(std::move(irradianceTexture));
        }

        for(auto i = 0; i < environments.size(); ++i) {
            Texture specularTexture;
            specularTexture.bindingId = environments.size() * 2 + i + bindingOffset;
            specularTexture.levels = 5;
            textures::createNoTransition(device, specularTexture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {512, 512, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
            createSpecularMap(environments[i], specularTexture);
            specularMaps.push_back(std::move(specularTexture));
        }

        for(auto i = 0; i < environments.size(); ++i) {
            Texture charlieTexture;
            charlieTexture.bindingId = environments.size() * 3 + i + bindingOffset;
            charlieTexture.levels = 5;
            textures::createNoTransition(device, charlieTexture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {512, 512, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
            createCharlieMap(environments[i], charlieTexture);
            charlieMaps.push_back(std::move(charlieTexture));
        }

        uniforms.irradiance_texture_id = irradianceMaps[options.environment].bindingId;
        uniforms.specular_texture_id = specularMaps[options.environment].bindingId;
        uniforms.charlie_env_texture_id = charlieMaps[options.environment].bindingId;


    });

}

void GltfViewer::createDescriptorSetLayouts() {
    convolution.inDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("irradiance_convolution_in")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    convolution.outDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("irradiance_convolution_out")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    uniformsDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("uniforms_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
}

void GltfViewer::updateDescriptorSets(){
    const uint32_t numDescriptorSets = swapChainImageCount * 2;
    std::vector<VulkanDescriptorSetLayout> setLayouts(numDescriptorSets, uniformsDescriptorSetLayout);
    std::vector<VkDescriptorBufferInfo> infos(numDescriptorSets);
    auto sets = descriptorPool.allocate({ setLayouts });

    std::vector<VkWriteDescriptorSet> writes(numDescriptorSets, { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET });

    for(auto i = 0; i < swapChainImageCount; ++i) {
        gBuffer.UniformsDescriptorSet[i] = sets[i];
        writes[i].dstSet = gBuffer.UniformsDescriptorSet[i];
        writes[i].dstBinding = 0;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[i].descriptorCount = 1;
        infos[i] = VkDescriptorBufferInfo{gBuffer.uniforms[i], 0, VK_WHOLE_SIZE};
        writes[i].pBufferInfo = &infos[i];
    }

    for(auto i = 0; i < swapChainImageCount; ++i) {
        transmissionFramebuffer.UniformsDescriptorSet[i] = sets[i + swapChainImageCount];
        writes[i + swapChainImageCount].dstSet = transmissionFramebuffer.UniformsDescriptorSet[i];
        writes[i + swapChainImageCount].dstBinding = 0;
        writes[i + swapChainImageCount].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[i + swapChainImageCount].descriptorCount = 1;
        infos[i + swapChainImageCount] = VkDescriptorBufferInfo{transmissionFramebuffer.uniforms[i], 0, VK_WHOLE_SIZE};
        writes[i + swapChainImageCount].pBufferInfo = &infos[i + swapChainImageCount];
    }

    device.updateDescriptorSets(writes);
    
}

void GltfViewer::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);

    computeCommandPool = device.createCommandPool(*device.queueFamilyIndex.compute, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, 1);
}

void GltfViewer::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void GltfViewer::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.environmentMap.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("environment_map.vert.spv"))
                    .fragmentShader(resource("environment_map.frag.spv"))
                .vertexInputState().clear()
                    .addVertexBindingDescription(0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX)
                    .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
                .rasterizationState()
                    .cullFrontFace()
                .depthStencilState()
                    .compareOpLessOrEqual()
                .dynamicRenderPass()
                    .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                    .depthAttachment(VK_FORMAT_D16_UNORM)
                .layout().clear()
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                    .addDescriptorSetLayout(uniformsDescriptorSetLayout)
                .name("environment")
                .build(render.environmentMap.layout);

        auto builder1 = prototypes->cloneGraphicsPipeline();
        render.pbr.pipeline =
            builder1
                .shaderStage()
                    .vertexShader(resource("pbr.vert.spv"))
                    .fragmentShader(resource("pbr.frag.spv"))
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
                .dynamicState()
                    .colorBlendEnable()
                .dynamicRenderPass()
                    .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                    .depthAttachment(VK_FORMAT_D16_UNORM)
                .layout().clear()
                    .addDescriptorSetLayout(loader->descriptorSetLayout())
                    .addDescriptorSetLayout(loader->materialDescriptorSetLayout())
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                    .addDescriptorSetLayout(uniformsDescriptorSetLayout)
                .name("pbr_renderer")
                .build(render.pbr.layout);

        auto builder2 = prototypes->cloneGraphicsPipeline();
        render.toneMapper.pipeline =
            builder2
                .shaderStage()
                    .vertexShader(resource("tone_mapper.vert.spv"))
                    .fragmentShader(resource("tone_mapper.frag.spv"))
                .vertexInputState().clear()
                    .addVertexBindingDescriptions(ClipSpace::bindingDescription())
                    .addVertexAttributeDescriptions(ClipSpace::attributeDescriptions())
                .inputAssemblyState()
                    .triangleStrip()
                .rasterizationState()
                    .cullNone()
                .depthStencilState()
                    .disableDepthWrite()
                    .disableDepthTest()
                .layout().clear()
                    .addDescriptorSetLayout(uniformsDescriptorSetLayout)
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                .name("tone_mapper")
                .build(render.toneMapper.layout);

        //    @formatter:on
}

void GltfViewer::createComputePipeline() {

    // IRRADIANCE MAPP CONVOLUTION
    auto module = device.createShaderModule(resource("compute_irradiance_from_equirectangular_map.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute.irradiance.layout = device.createPipelineLayout({ convolution.inDescriptorSetLayout, convolution.outDescriptorSetLayout });

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.irradiance.layout.handle;

    compute.irradiance.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);

    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("irradiance_convolution_layout", compute.irradiance.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("irradiance_convolution_pipeline", compute.irradiance.pipeline.handle);

    // PANOROMA TO OCTAHEDRAL MAP CONVERSION
    module = device.createShaderModule(resource("equi_rectangular_to_octahedral_map.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    computeCreateInfo.stage = stage;
    compute.toOctahedralMap.layout = device.createPipelineLayout({ convolution.inDescriptorSetLayout, convolution.outDescriptorSetLayout });
    computeCreateInfo.layout = compute.toOctahedralMap.layout.handle;

    compute.toOctahedralMap.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);

    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("panoroma_to_octahedral_map_layout", compute.toOctahedralMap.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("panoroma_to_octahedral_map_pipeline", compute.toOctahedralMap.pipeline.handle);

    // SPECULAR MAP CONVOLUTION
    module = device.createShaderModule(resource("compute_specular_map.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    computeCreateInfo.stage = stage;
    compute.specular.layout = device.createPipelineLayout({ convolution.inDescriptorSetLayout, convolution.outDescriptorSetLayout }, { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float)} });
    computeCreateInfo.layout = compute.specular.layout.handle;

    compute.specular.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);

    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("specular_map_convolution_layout", compute.specular.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("specular_map_convolution_pipeline", compute.specular.pipeline.handle);

    // CHARLIE MAP CONVOLUTION
    module = device.createShaderModule(resource("compute_charlie_map.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    computeCreateInfo.stage = stage;
    compute.charlie.layout = device.createPipelineLayout({ convolution.inDescriptorSetLayout, convolution.outDescriptorSetLayout }, { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float)} });
    computeCreateInfo.layout = compute.charlie.layout.handle;

    compute.charlie.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);

    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("charlie_map_convolution_layout", compute.charlie.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("charlie_map_convolution_pipeline", compute.charlie.pipeline.handle);
}

void GltfViewer::onSwapChainDispose() {
    dispose(render.environmentMap.pipeline);
}

void GltfViewer::onSwapChainRecreation() {
    createGBuffer();
    createFrameBufferTexture();
    updateDescriptorSets();
    createRenderPipeline();
}

void GltfViewer::newFrame() {
    uniforms.framebuffer_texture_id = transmissionFramebuffer.color[currentImageIndex].bindingId;
    uniforms.g_buffer_texture_id = gBuffer.color[currentImageIndex].bindingId;
}

VkCommandBuffer *GltfViewer::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    renderToTransmissionFrameBuffer(commandBuffer);
    renderToGBuffer(commandBuffer);

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

    toneMap(commandBuffer);
    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void GltfViewer::updateUniforms(VkCommandBuffer commandBuffer, VulkanBuffer& uniformBuffer, const UniformData& data) {
    vkCmdUpdateBuffer(commandBuffer, uniformBuffer, 0, sizeof(UniformData), &data);
    VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.buffer = uniformBuffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 1, &barrier, 0, 0);

}

void GltfViewer::renderToTransmissionFrameBuffer(VkCommandBuffer commandBuffer) {
    static UniformData uniformData;
    uniformData = uniforms;
    uniformData.discard_transmissive = 1;
    uniformData.tone_map = 0;
    updateUniforms(commandBuffer, transmissionFramebuffer.uniforms[currentImageIndex], uniformData);

    offscreen.render(commandBuffer, transmissionFramebuffer.info[currentImageIndex], [&]{
        renderEnvironmentMap(commandBuffer, transmissionFramebuffer.UniformsDescriptorSet[currentImageIndex], &render.environmentMap.pipeline, &render.environmentMap.layout);
        renderModel(commandBuffer, transmissionFramebuffer.UniformsDescriptorSet[currentImageIndex], &render.pbr.pipeline, &render.pbr.layout, VK_FALSE);
    });
    textures::generateLOD(commandBuffer, transmissionFramebuffer.color[currentImageIndex].image, transmissionFramebuffer.color[currentImageIndex].width
                          , transmissionFramebuffer.color[currentImageIndex].height, transmissionFramebuffer.color[currentImageIndex].levels);
}

void GltfViewer::renderToGBuffer(VkCommandBuffer commandBuffer) {
    updateUniforms(commandBuffer, gBuffer.uniforms[currentImageIndex], uniforms);

    offscreen.render(commandBuffer, gBuffer.info[currentImageIndex], [&]{
        renderEnvironmentMap(commandBuffer, gBuffer.UniformsDescriptorSet[currentImageIndex], &render.environmentMap.pipeline, &render.environmentMap.layout);
        renderModel(commandBuffer, gBuffer.UniformsDescriptorSet[currentImageIndex], &render.pbr.pipeline, &render.pbr.layout, VK_TRUE);
    });
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.image = gBuffer.color[currentImageIndex].image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.subresourceRange = DEFAULT_SUB_RANGE;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 0, 0, 1, &barrier);

}

void GltfViewer::toneMap(VkCommandBuffer commandBuffer){
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = gBuffer.UniformsDescriptorSet[currentImageIndex];
    sets[1] = bindlessDescriptor.descriptorSet;
    VkDeviceSize offset = 0;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.toneMapper.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.toneMapper.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, screenQuad, &offset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void GltfViewer::renderEnvironmentMap(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet, VulkanPipeline* pipeline, VulkanPipelineLayout* layout) {

    pipeline = !pipeline ? &render.environmentMap.pipeline : pipeline;
    layout = !layout ? &render.environmentMap.layout : layout;

    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = bindlessDescriptor.descriptorSet;
    sets[1] = descriptorSet;
    VkDeviceSize offset = 0;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout->handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, skyBox.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, skyBox.indices, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, skyBox.indices.sizeAs<uint32_t>(), 1, 0, 0, 0);
}

void GltfViewer::renderModel(VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet, VulkanPipeline* pipeline, VulkanPipelineLayout* layout, VkBool32 blendingEnabled) {
    auto model = models[currentModel];
    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = model->meshDescriptorSet.u16.handle;
    sets[1] = model->materialDescriptorSet;
    sets[2] = bindlessDescriptor.descriptorSet;
    sets[3] = descriptorSet;

    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout->handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, model->vertices, &offset);

    vkCmdSetColorBlendEnableEXT(commandBuffer, 0, 1, &blendingEnabled);
    vkCmdBindIndexBuffer(commandBuffer, model->indices.u16.handle, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexedIndirect(commandBuffer, model->draw.u16.handle, 0, model->draw.u16.count, sizeof(VkDrawIndexedIndirectCommand));

    sets[0] = model->meshDescriptorSet.u32.handle;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout->handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindIndexBuffer(commandBuffer, model->indices.u32.handle, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexedIndirect(commandBuffer, model->draw.u32.handle, 0, model->draw.u32.count, sizeof(VkDrawIndexedIndirectCommand));

    sets[0] = model->meshDescriptorSet.u8.handle;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout->handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindIndexBuffer(commandBuffer, model->indices.u8.handle, 0, VK_INDEX_TYPE_UINT8_EXT);
    vkCmdDrawIndexedIndirect(commandBuffer, model->draw.u8.handle, 0, model->draw.u8.count, sizeof(VkDrawIndexedIndirectCommand));
}

void GltfViewer::renderUI(VkCommandBuffer commandBuffer) {
    if(ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if(ImGui::MenuItem("Open File...", nullptr, false, true)) {
                FileDialog::file_dialog_open = true;
                FileDialog::file_dialog_open_type = FileDialog::FileDialogType::OpenFile;
            }

            if(ImGui::MenuItem("Exit")){
                this->exit->press();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    ImGui::Begin("Settings");
    ImGui::SetWindowSize({0, 0});

    ImGui::Text("Sample Models");
    ImGui::PushID("model_selection");
    options.modelSelected = ImGui::Combo("", &options.selectedModel, options.models.data(), options.models.size());
    ImGui::PopID();


    ImGui::Text(""); // there is probably a layout for spacing, I'm just being lazy for now :)
    ImGui::Text("Environments");
    ImGui::Combo("", &options.environment, environmentPaths.data(), environmentPaths.size());
    static bool showEnv = true;
    ImGui::SameLine();
    ImGui::Checkbox("Show", &showEnv);
    options.envMapType = showEnv ? 0 : 1;

    ImGui::Text(""); // there is probably a layout for spacing, I'm just being lazy for now :)
    ImGui::Text("Lighting");
    ImGui::Checkbox("Direct", &options.directLighting);
    ImGui::SameLine();
    ImGui::Checkbox("IBL", &options.imageBasedLighting);
    ImGui::SliderFloat("IBL intensity", &options.iblIntensity, 1, 1000, "%.0f", ImGuiSliderFlags_Logarithmic);

    ImGui::Text(""); // there is probably a layout for spacing, I'm just being lazy for now :)
    ImGui::Text("Cameras");
    ImGui::PushID("camera");
    ImGui::Combo("", &options.camera, options.cameras.data(), options.cameras.size());
    ImGui::PopID();

    ImGui::Text(""); // there is probably a layout for spacing, I'm just being lazy for now :)
    ImGui::Text("Debug");
    ImGui::RadioButton("Off", &options.debug, 0); ImGui::SameLine();
    ImGui::RadioButton("Color", &options.debug, 1); ImGui::SameLine();
    ImGui::RadioButton("Normal", &options.debug, 2); ImGui::SameLine();
    ImGui::RadioButton("Metalness", &options.debug, 3); ImGui::SameLine();
    ImGui::RadioButton("Roughness", &options.debug, 4);
    ImGui::RadioButton("AmbientOcclusion", &options.debug, 5); ImGui::SameLine();
    ImGui::RadioButton("Emission", &options.debug, 6); ImGui::SameLine();

    ImGui::End();

    if(fileOpen.error) {
        ImGui::Begin("Error");
        ImGui::SetWindowSize({300, 100});
        ImGui::Text("%s", fileOpen.message.c_str());

        if(ImGui::Button("Close")) {
            fileOpen.error = false;
            fileOpen.message.clear();
        }
        ImGui::End();
    }

    openFileDialog();
    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void GltfViewer::update(float time) {
    if(!(ImGui::IsAnyItemActive() || ImGui::IsAnyItemHovered()) && !FileDialog::file_dialog_open && options.camera == 0) {
        camera->update(time);
    }
    auto cam = camera->cam();
}

void GltfViewer::checkAppInputs() {
    if(!ImGui::IsAnyItemActive() && !FileDialog::file_dialog_open && options.camera == 0) {
        camera->processInput();
    }
}

void GltfViewer::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void GltfViewer::onPause() {
    VulkanBaseApp::onPause();
}

void GltfViewer::openFileDialog() {
    static char* file_dialog_buffer = nullptr;
    static char path[500] =  R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\dependencies\glTF-Sample-Assets\Models)";
//    static char path[500] =  "";

    file_dialog_buffer = path;

    static bool closed = false;
    if (FileDialog::file_dialog_open) {
        FileDialog::ShowFileDialog(&FileDialog::file_dialog_open, file_dialog_buffer, &closed, FileDialog::file_dialog_open_type);
    }

    if(closed) {
        gltfPath = std::filesystem::path{file_dialog_buffer};
        if(std::filesystem::is_directory(*gltfPath)){
            fileOpen.error = true;
            fileOpen.message = "you tried to open a directory";
            gltfPath.reset();
        }else if(gltfPath->extension().string() != ".gltf"){
            fileOpen.error = true;
            fileOpen.message = "selected file is not a supported gltf format";
            gltfPath.reset();
        }
        FileDialog::file_dialog_open = fileOpen.error;
        closed = false;
    }
}

void GltfViewer::createSkyBox() {
    auto cube = primitives::cube();
    std::vector<glm::vec3> vertices;
    for(const auto& vertex : cube.vertices){
        vertices.push_back(vertex.position.xyz());
    }
    skyBox.vertices = device.createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    skyBox.indices = device.createDeviceLocalBuffer(cube.indices.data(), BYTE_SIZE(cube.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void GltfViewer::convertToOctahedralMap(Texture &srcTexture, Texture &dstTexture) {
    updateConvolutionDescriptors(srcTexture, dstTexture);

    Synchronization synchronization{ ._fence = device.createFence() };
    synchronization._fence.reset();

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        std::array<VkDescriptorSet, 2> sets{ convolution.inDescriptorSet[0], convolution.outDescriptorSet[0] };
        glm::uvec3 gc{ dstTexture.width/8, dstTexture.height/8, 1 };

        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.image = dstTexture.image.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, dstTexture.levels, 0, 1};
        barrier.srcAccessMask = VK_ACCESS_NONE;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);


        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.toOctahedralMap.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.toOctahedralMap.layout.handle
                , 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);

        vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);

        dstTexture.image.currentLayout = VK_IMAGE_LAYOUT_GENERAL;
//        textures::generateLOD(commandBuffer, dstTexture.image, dstTexture.width, dstTexture.height, dstTexture.levels);

    }, synchronization);
    synchronization._fence.wait();
    dstTexture.path = srcTexture.path;
    bindlessDescriptor.update({ &dstTexture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, dstTexture.bindingId });
    spdlog::info("{} converted to octahedral map", srcTexture.path);
}

void GltfViewer::createIrradianceMap(Texture &srcTexture, Texture &dstTexture) {
    updateConvolutionDescriptors(srcTexture, dstTexture, VK_IMAGE_LAYOUT_GENERAL);

    Synchronization synchronization{};
    synchronization._fence = device.createFence();

    synchronization._fence.reset();
    computeCommandPool.oneTimeCommand([&](auto commandBuffer){
        std::array<VkDescriptorSet, 2> sets{ convolution.inDescriptorSet[0], convolution.outDescriptorSet[0] };
        glm::uvec3 gc{ dstTexture.width/8 , dstTexture.height/8, 1 };

        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.image = dstTexture.image.image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = VK_ACCESS_NONE;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);


        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.irradiance.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.irradiance.layout.handle
                , 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);

        vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);

    }, synchronization);
    synchronization._fence.wait();
    bindlessDescriptor.update({ &dstTexture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, dstTexture.bindingId });
    spdlog::info("irradiance convolution completed for environment map: {}", srcTexture.path);
}

void GltfViewer::createSpecularMap(Texture &srcTexture, Texture &dstTexture) {

    std::array<VulkanImageView, 6> imageViews{};
    imageViews[0] = srcTexture.imageView;

    VkImageSubresourceRange subresourceRange;
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = 1;

    for(auto i = 0; i < 5; ++i) {
        subresourceRange.baseMipLevel = i;
        subresourceRange.levelCount = 5 - i;
        imageViews[i + 1] = dstTexture.image.createView(VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);
    }

    updateSpecularDescriptors(imageViews);

    Synchronization synchronization{};
    synchronization._fence = device.createFence();
    synchronization._fence.reset();
    computeCommandPool.oneTimeCommand([&](auto commandBuffer){
        static std::array<VkDescriptorSet, 2> sets;


        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.image = dstTexture.image;
        barrier.subresourceRange = subresourceRange;

        for(auto i = 0; i < dstTexture.levels; i++ ){
            const auto width = dstTexture.width >> i;
            const auto height = dstTexture.height >> i;
            float roughness = static_cast<float>(i) / static_cast<float>(dstTexture.levels - 1);

            barrier.srcAccessMask = VK_ACCESS_NONE;
            barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.subresourceRange.baseMipLevel = i;
            barrier.subresourceRange.levelCount = dstTexture.levels - i;

            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                    , 0, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 1, &barrier);

            sets[0] = convolution.inDescriptorSet[i];
            sets[1] = convolution.outDescriptorSet[i];

            spdlog::info("generating specular map for {}, mip level {}, roughness {}, size {}", srcTexture.path, i, roughness, width);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.specular.pipeline.handle);
            vkCmdPushConstants(commandBuffer, compute.specular.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float), &roughness);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.specular.layout.handle , 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);

            vkCmdDispatch(commandBuffer, width, height, 1);

            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;

            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                    , 0, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 1, &barrier);
        }
    }, synchronization);

    synchronization._fence.wait();
    dstTexture.path = srcTexture.path;
    bindlessDescriptor.update({ &dstTexture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, dstTexture.bindingId });
    spdlog::info("Specular convolution completed for environment map: {}", srcTexture.path);
}

void GltfViewer::createCharlieMap(Texture &srcTexture, Texture &dstTexture) {

    std::array<VulkanImageView, 6> imageViews{};
    imageViews[0] = srcTexture.imageView;

    VkImageSubresourceRange subresourceRange;
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = 0;
    subresourceRange.layerCount = 1;

    for(auto i = 0; i < 5; ++i) {
        subresourceRange.baseMipLevel = i;
        subresourceRange.levelCount = 5 - i;
        imageViews[i + 1] = dstTexture.image.createView(VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);
    }

    updateSpecularDescriptors(imageViews);

    Synchronization synchronization{};
    synchronization._fence = device.createFence();
    synchronization._fence.reset();
    computeCommandPool.oneTimeCommand([&](auto commandBuffer){
        static std::array<VkDescriptorSet, 2> sets;


        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.image = dstTexture.image;
        barrier.subresourceRange = subresourceRange;

        for(auto i = 0; i < dstTexture.levels; i++ ){
            const auto width = dstTexture.width >> i;
            const auto height = dstTexture.height >> i;
            float roughness = static_cast<float>(i) / static_cast<float>(dstTexture.levels - 1);

            barrier.srcAccessMask = VK_ACCESS_NONE;
            barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.subresourceRange.baseMipLevel = i;
            barrier.subresourceRange.levelCount = dstTexture.levels - i;

            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                    , 0, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 1, &barrier);

            sets[0] = convolution.inDescriptorSet[i];
            sets[1] = convolution.outDescriptorSet[i];

            spdlog::info("generating charlie map for {}, mip level {}, roughness {}, size {}", srcTexture.path, i, roughness, width);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.charlie.pipeline.handle);
            vkCmdPushConstants(commandBuffer, compute.charlie.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float), &roughness);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.charlie.layout.handle , 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);

            vkCmdDispatch(commandBuffer, width, height, 1);

            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;

            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                    , 0, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, 1, &barrier);
        }
    }, synchronization);

    synchronization._fence.wait();
    dstTexture.path = srcTexture.path;
    bindlessDescriptor.update({ &dstTexture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, dstTexture.bindingId });
    spdlog::info("Specular convolution completed for environment map: {}", srcTexture.path);
}


void GltfViewer::updateConvolutionDescriptors(Texture &srcTexture, Texture &dstTexture, VkImageLayout srcImageLayout) {
    auto sets = descriptorPool.allocate({ convolution.inDescriptorSetLayout, convolution.outDescriptorSetLayout });
    convolution.inDescriptorSet[0] =  sets[0];
    convolution.outDescriptorSet[0] =  sets[1];
    
    auto writes = initializers::writeDescriptorSets<2>();
    
    writes[0].dstSet = convolution.inDescriptorSet[0];
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo inInfo{ srcTexture.sampler.handle, srcTexture.imageView.handle, srcImageLayout };
    writes[0].pImageInfo = &inInfo;

    writes[1].dstSet = convolution.outDescriptorSet[0];
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo outInfo{ dstTexture.sampler.handle, dstTexture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[1].pImageInfo = &outInfo;

    device.updateDescriptorSets(writes);
}

void GltfViewer::updateSpecularDescriptors(const std::array<VulkanImageView, 6> &imageViews) {

    for(auto i = 0; i < 5; ++i){
        auto sets = descriptorPool.allocate({ convolution.inDescriptorSetLayout, convolution.outDescriptorSetLayout });
        convolution.inDescriptorSet[i] =  sets[0];
        convolution.outDescriptorSet[i] =  sets[1];

        auto writes = initializers::writeDescriptorSets<2>();
        writes[0].dstSet = convolution.inDescriptorSet[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        VkDescriptorImageInfo inInfo{ convolution.sampler.handle, imageViews[i].handle, VK_IMAGE_LAYOUT_GENERAL };
        writes[0].pImageInfo = &inInfo;

        writes[1].dstSet = convolution.outDescriptorSet[i];
        writes[1].dstBinding = 0;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1].descriptorCount = 1;
        VkDescriptorImageInfo outInfo{ VK_NULL_HANDLE, imageViews[i + 1 % 5].handle, VK_IMAGE_LAYOUT_GENERAL };
        writes[1].pImageInfo = &outInfo;

        device.updateDescriptorSets(writes);
    }
}

void GltfViewer::initModels() {
    auto dummyModel = loader->dummyModel();
    models[0] = dummyModel;
    models[1] = dummyModel;
}

void GltfViewer::endFrame() {

    if(options.modelSelected) {
        gltfPath = modelPaths[options.models[options.selectedModel]];
        options.modelSelected = false;
    }

    if(gltfPath.has_value()) {
        currentModel = (currentModel + 1) % static_cast<int>(models.size());
        models[currentModel] = loader->loadGltf(*gltfPath);
        uniforms.num_lights = models[currentModel]->numLights;
        camera->updateModel(models[currentModel]->bounds.min, models[currentModel]->bounds.max);

        if(!models[currentModel]->cameras.empty()) {
            options.cameras.clear();
            options.cameras.push_back("Default");
            for(auto i = 0; i < models[currentModel]->cameras.size(); ++i){
                options.cameras.push_back(_strdup(std::to_string(i).c_str()));
            }
            options.camera = 0;
        }

        gltfPath.reset();
    }

    static int previousEnvironment = options.environment;

    if(previousEnvironment != options.environment){
        uniforms.irradiance_texture_id = irradianceMaps[options.environment].bindingId;
        uniforms.specular_texture_id = specularMaps[options.environment].bindingId;
        uniforms.charlie_env_texture_id = charlieMaps[options.environment].bindingId;
        previousEnvironment = options.environment;
    }
    uniforms.camera = options.camera == 0 ? camera->cam() : models[currentModel]->cameras[options.camera - 1];
    uniforms.environment = environments[options.environment].bindingId;
    uniforms.debug = options.debug;
    uniforms.direct_on = int(options.directLighting);
    uniforms.ibl_on = int(options.imageBasedLighting);
    uniforms.ibl_intensity = options.iblIntensity;

    if(options.envMapType == 1) {
        uniforms.environment = irradianceMaps[options.environment].bindingId;
    }
}

void GltfViewer::constructModelPaths() {
    std::set<std::string> seen;
    for(auto const& entry : fs::recursive_directory_iterator{ resource("Models") }) {
        if(!fs::is_directory(entry.path())) {
            try {
                const auto extension = entry.path().extension().string();
                if (extension == ".gltf") {
                    std::string name = entry.path().filename().string();
                    name = name.substr(0, name.size() - extension.size());

                    if(seen.contains(name)) continue;
                    seen.insert(name);

                    modelPaths.insert(std::make_pair(name, entry.path().string()));
                    options.models.push_back(_strdup(name.c_str()));
                }
            }catch(...) {
                // UTF-8 characters cause exception, maybe look at wide strings
            }
        }
    }
}

int main(){
    try{

        Settings settings;
        settings.width = 1440;
        settings.height = 1280;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = GltfViewer{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}
