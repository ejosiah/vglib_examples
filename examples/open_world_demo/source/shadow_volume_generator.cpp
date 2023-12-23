//#include "shadow_volume_generator.hpp"
//#include "GraphicsPipelineBuilder.hpp"
//#include <meshoptimizer.h>
//
//ShadowVolumeGenerator::ShadowVolumeGenerator(const VulkanDevice &device, const VulkanDescriptorPool &descriptorPool,
//                                             const FileManager &fileManager, uint32_t width, uint32_t height, VulkanRenderPass& renderPass)
//
//        :m_device{&device}
//        ,m_descriptorPool{&descriptorPool}
//        , m_filemanager(&fileManager)
//        , m_width{width}
//        , m_height{ height }
//        , m_renderPass{ &renderPass }
//{
//    initUBO();
//    initSamplers();
//    createFramebufferAttachments();
//    createFrameBuffer();
//    createDescriptorSetLayout();
//    updateDescriptorSet();
//    createPipelines();
//}
//
//
//void ShadowVolumeGenerator::generate(const SceneData& sceneData, const VulkanBuffer &sourceBuffer, int triangleCount) {
//    ubo->model = sceneData.camera.model;
//    ubo->view = sceneData.camera.view;
//    ubo->proj = sceneData.camera.proj;
//    ubo->lightPosition = sceneData.sun.position;
//    ubo->cameraPosition = sceneData.eyes;
//
//    device().graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
//
//        static std::array<VkClearValue, 4> clearValues;
//        clearValues[0].depthStencil = {1.0, 0u};
//        clearValues[1].depthStencil = {0.0, 0u};
//        clearValues[2].color = {0, 0, 0, 0};
//        clearValues[3].color = {0, 0, 0, 0};
//
//        VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
//        rPassInfo.clearValueCount = COUNT(clearValues);
//        rPassInfo.pClearValues = clearValues.data();
//        rPassInfo.framebuffer = m_shadowVolumeFramebuffer;
//        rPassInfo.renderArea.offset = {0u, 0u};
//        rPassInfo.renderArea.extent = { m_width, m_height};
//        rPassInfo.renderPass = m_shadowVolumeRenderPass;
//
//        vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);
//        static std::array<VkDescriptorSet, 1> sets;
//        sets[0] = descriptorSet;
//
//        VkDeviceSize offset = 0;
//        vkCmdBindIndexBuffer(commandBuffer, adjacencySupport.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
//        vkCmdBindVertexBuffers(commandBuffer, 0, 1, adjacencySupport.triangleBuffer, &offset);
//
//        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, subpasses.shadowVolumeFront.pipeline);
//        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, subpasses.shadowVolumeFront.layout, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
//        vkCmdDrawIndexed(commandBuffer, adjacencySupport.numIndices, 1, 0, 0, 0);
//
//        vkCmdNextSubpass(commandBuffer, VK_SUBPASS_CONTENTS_INLINE);
//        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, subpasses.shadowVolumeBack.pipeline);
//        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, subpasses.shadowVolumeBack.layout, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
//        vkCmdDrawIndexed(commandBuffer, adjacencySupport.numIndices, 1, 0, 0, 0);
//
//        vkCmdEndRenderPass(commandBuffer);
//
//    });
//}
//
//void ShadowVolumeGenerator::update(const SceneData &sceneData) {
//    camera = sceneData.camera;
//    ubo->model = sceneData.camera.model;
//    ubo->view = sceneData.camera.view;
//    ubo->proj = sceneData.camera.proj;
//    ubo->lightPosition = sceneData.sun.position;
//    ubo->cameraPosition = sceneData.eyes;
//}
//
//void ShadowVolumeGenerator::render(VkCommandBuffer commandBuffer) {
//    VkDeviceSize offset = 0;
//    static std::array<VkDescriptorSet, 1> sets;
//    sets[0] = descriptorSet;
//
//    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_volume_visual.pipeline);
//    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadow_volume_visual.layout, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
//    vkCmdBindVertexBuffers(commandBuffer, 0, 1, adjacencySupport.triangleBuffer, &offset);
//    vkCmdBindIndexBuffer(commandBuffer, adjacencySupport.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
//    vkCmdDrawIndexed(commandBuffer, adjacencySupport.numIndices, 1, 0, 0, 0);
//}
//
//void ShadowVolumeGenerator::createFramebufferAttachments() {
//    shadowVolume = std::make_shared<ShadowVolume>();
//
//    VkImageSubresourceRange subresourceRange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
//    VkImageCreateInfo info = initializers::imageCreateInfo(
//            VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT  , m_width, m_height);
//
//
//    depthBufferIn.image = device().createImage(info);
//    depthBufferIn.imageView = depthBufferIn.image.createView(info.format, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);
//    device().setName<VK_OBJECT_TYPE_IMAGE>("depth_in_image", depthBufferIn.image);
//
//    depthBufferOut.image = device().createImage(info);
//    depthBufferOut.imageView = depthBufferOut.image.createView(info.format, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);
//    device().setName<VK_OBJECT_TYPE_IMAGE>("depth_out_image", depthBufferOut.image);
//
//    info.format = VK_FORMAT_R32G32B32A32_SFLOAT;
//    info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT  | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
//    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//    shadowVolume->shadowIn.image = device().createImage(info);
//    shadowVolume->shadowIn.imageView = shadowVolume->shadowIn.image.createView(info.format, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);
//    device().setName<VK_OBJECT_TYPE_IMAGE>("shadow_in_image", shadowVolume->shadowIn.image);
//
//    info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
//    shadowVolume->shadowOut.image = device().createImage(info);
//    shadowVolume->shadowOut.imageView = shadowVolume->shadowOut.image.createView(info.format, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);
//    device().setName<VK_OBJECT_TYPE_IMAGE>("shadow_out_image", shadowVolume->shadowOut.image);
//
//    device().graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
//        shadowVolume->shadowOut.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
//                                                      subresourceRange, VK_ACCESS_NONE, VK_ACCESS_SHADER_READ_BIT,
//                                                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
//    });
//}
//
//void ShadowVolumeGenerator::createFrameBuffer() {
//    VkAttachmentDescription attachment{
//            0,
//            VK_FORMAT_D32_SFLOAT,
//            VK_SAMPLE_COUNT_1_BIT,
//            VK_ATTACHMENT_LOAD_OP_CLEAR,
//            VK_ATTACHMENT_STORE_OP_STORE,
//            VK_ATTACHMENT_LOAD_OP_DONT_CARE,
//            VK_ATTACHMENT_STORE_OP_DONT_CARE,
//            VK_IMAGE_LAYOUT_UNDEFINED,
//            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
//    };
//
//    std::vector<VkAttachmentDescription> attachmentDesc;
//    attachmentDesc.push_back(attachment);
//    attachmentDesc.push_back(attachment);
//
//    attachment.format = VK_FORMAT_R32G32B32A32_SFLOAT;
//    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
//    attachmentDesc.push_back(attachment);
//
//
//    attachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
//    attachmentDesc.push_back(attachment);
//
//    std::vector<SubpassDescription> subpasses(2);
//
//    subpasses[0].depthStencilAttachments = {0, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL};
//    subpasses[0].colorAttachments.push_back({2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
//
//    subpasses[1].depthStencilAttachments = {1, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL};
//    subpasses[1].inputAttachments.push_back({2, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
//    subpasses[1].colorAttachments.push_back({3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
//
//
//    std::vector<VkSubpassDependency> dependencies{
//            {
//                VK_SUBPASS_EXTERNAL,
//                0,
//                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
//                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
//                VK_ACCESS_SHADER_READ_BIT,
//                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
//                0
//            },
//            {
//                0,
//                1,
//                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
//                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
//                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
//                VK_ACCESS_SHADER_READ_BIT
//            },
//            {
//                1,
//                VK_SUBPASS_EXTERNAL,
//                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
//                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
//                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
//                VK_ACCESS_SHADER_READ_BIT,
//                0
//            }
//    };
//
//    m_shadowVolumeRenderPass = device().createRenderPass(attachmentDesc, subpasses, dependencies);
//    device().setName<VK_OBJECT_TYPE_RENDER_PASS>("shadow_volume_render_pass", m_shadowVolumeRenderPass.renderPass);
//
//    std::vector<VkImageView> attachments {
//        depthBufferIn.imageView,
//        depthBufferOut.imageView,
//        shadowVolume->shadowIn.imageView,
//        shadowVolume->shadowOut.imageView,
//    };
//
//    m_shadowVolumeFramebuffer = device().createFramebuffer(m_shadowVolumeRenderPass, attachments, m_width, m_height);
//    device().setName<VK_OBJECT_TYPE_FRAMEBUFFER>("shadow_volume_framebuffer", m_shadowVolumeFramebuffer.frameBuffer);
//}
//
//void ShadowVolumeGenerator::initUBO() {
//    uboBuffer = device().createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(Ubo));
//    ubo = reinterpret_cast<Ubo*>(uboBuffer.map());
//}
//
//void ShadowVolumeGenerator::initAdjacencyBuffers(const VulkanBuffer &sourceBuffer, int triangleCount) {
//    auto indexCount = triangleCount * 3;
//
//    VulkanBuffer stagingBuffer = device().createStagingBuffer(indexCount * sizeof(Vertex));
//    device().copy(sourceBuffer, stagingBuffer, indexCount * sizeof(Vertex));
//
//    std::vector<Vertex> vertices(indexCount);
//    auto source = reinterpret_cast<Vertex*>(stagingBuffer.map());
//    std::memcpy(vertices.data(), source, sizeof(Vertex) * indexCount);
//
//    std::vector<uint32_t> remap(indexCount);
//    auto vertexCount = meshopt_generateVertexRemap(remap.data(), nullptr, indexCount, vertices.data(), vertices.size(), sizeof(Vertex));
//
//    std::vector<uint32_t> indices(indexCount);
//    meshopt_remapIndexBuffer(indices.data(),  nullptr, indexCount, remap.data());
//
//    auto unIndexedVertices = vertices;
//    vertices.resize(vertexCount);
//    meshopt_remapVertexBuffer(vertices.data(), unIndexedVertices.data(), indexCount, sizeof(Vertex), remap.data());
//    meshopt_optimizeVertexCache(indices.data(), indices.data(), indexCount, vertexCount);
//
//    std::vector<uint32_t> adjacencyIndices(indexCount * 2);
//    meshopt_generateAdjacencyIndexBuffer(adjacencyIndices.data(), indices.data(), indexCount,
//                                         reinterpret_cast<const float*>(vertices.data()), vertexCount, sizeof(Vertex));
//
//    indices = adjacencyIndices;
//    adjacencySupport.triangleBuffer = device().createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
//    adjacencySupport.indexBuffer = device().createDeviceLocalBuffer(indices.data(), BYTE_SIZE(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
//    adjacencySupport.numIndices =  indices.size();
//    sourceBuffer.unmap();
//
//}
//
//std::string ShadowVolumeGenerator::resource(const std::string &name) {
//    auto res = m_filemanager->getFullPath(name);
//    assert(res.has_value());
//    return res->string();
//}
//
//void ShadowVolumeGenerator::createDescriptorSetLayout() {
//    descriptorSetLayout =
//        device().descriptorSetLayoutBuilder()
//            .name("main")
//            .binding(0)
//                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
//                .descriptorCount(1)
//                .shaderStages(VK_SHADER_STAGE_GEOMETRY_BIT)
//            .binding(1)
//                .descriptorType(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
//                .descriptorCount(1)
//                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
//        .createLayout();
//
//    shadowVolume->setLayout =
//        device().descriptorSetLayoutBuilder()
//            .name("shadow_volume")
//                .binding(0)
//                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
//                .descriptorCount(1)
//                .immutableSamplers(valueSampler)
//                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
//            .createLayout();
//}
//
//void ShadowVolumeGenerator::updateDescriptorSet() {
//    auto sets = descriptorPool().allocate( {descriptorSetLayout, shadowVolume->setLayout } );
//    descriptorSet = sets[0];
//    shadowVolume->descriptorSet = sets[1];
//
//    auto writes = initializers::writeDescriptorSets<3>();
//    writes[0].dstSet = descriptorSet;
//    writes[0].dstBinding = 0;
//    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
//    writes[0].descriptorCount = 1;
//    VkDescriptorBufferInfo uboInfo{ uboBuffer, 0, VK_WHOLE_SIZE};
//    writes[0].pBufferInfo = &uboInfo;
//
//    writes[1].dstSet = descriptorSet;
//    writes[1].dstBinding = 1;
//    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
//    writes[1].descriptorCount = 1;
//    VkDescriptorImageInfo shadowInfo{VK_NULL_HANDLE, shadowVolume->shadowIn.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
//    writes[1].pImageInfo = &shadowInfo;
//
//    writes[2].dstSet = shadowVolume->descriptorSet;
//    writes[2].dstBinding = 0;
//    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
//    writes[2].descriptorCount = 1;
//    VkDescriptorImageInfo shadowOutInfo{VK_NULL_HANDLE, shadowVolume->shadowOut.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
//    writes[2].pImageInfo = &shadowOutInfo;
//
//    device().updateDescriptorSets(writes);
//}
//
//void ShadowVolumeGenerator::createPipelines() {
//	auto builder = device().graphicsPipelineBuilder();
//    subpasses.shadowVolumeFront.pipeline =
//        builder
//            .allowDerivatives()
//            .shaderStage()
//                .vertexShader(resource("shadow_volume.vert.spv"))
//                .geometryShader(resource("shadow_volume.geom.spv"))
//                .fragmentShader(resource("shadow_volume_in.frag.spv"))
//            .vertexInputState()
//                .addVertexBindingDescriptions(Vertex::bindingDisc())
//                .addVertexAttributeDescriptions(Vertex::attributeDisc())
//            .inputAssemblyState()
//                .trianglesWithAdjacency()
//            .viewportState()
//                .viewport()
//                    .origin(0, 0)
//                    .dimension(m_width, m_height)
//                    .minDepth(0)
//                    .maxDepth(1)
//                .scissor()
//                    .offset(0, 0)
//                    .extent(m_width, m_height)
//                .add()
//                .rasterizationState()
//                    .cullBackFace()
//                    .frontFaceCounterClockwise()
//                    .polygonModeFill()
//                    .enableDepthClamp()
//                .multisampleState()
//                    .rasterizationSamples(VK_SAMPLE_COUNT_1_BIT)
//                .depthStencilState()
//                    .enableDepthWrite()
//                    .enableDepthTest()
//                    .compareOpLess()
//                    .minDepthBounds(0)
//                    .maxDepthBounds(1)
//                .colorBlendState()
//                    .attachments(1)
//                .layout()
//                    .addDescriptorSetLayout(descriptorSetLayout)
//                .renderPass(m_shadowVolumeRenderPass)
//                .subpass(0)
//            .name("shadow_volume_in")
//        .build(subpasses.shadowVolumeFront.layout);
//
//    subpasses.shadowVolumeBack.pipeline =
//        builder
//            .basePipeline(subpasses.shadowVolumeFront.pipeline)
//            .shaderStage()
//                .fragmentShader(resource("shadow_volume.frag.spv"))
//            .rasterizationState()
//                .disableRasterizerDiscard()
//                .cullFrontFace()
//            .depthStencilState()
//                .compareOpGreater()
//            .subpass(1)
//        .name("shadow_volume_out")
//    .build(subpasses.shadowVolumeBack.layout);
//
//    shadow_volume_visual.pipeline =
//        builder
//            .shaderStage().clear()
//                .vertexShader(resource("shadow_volume.vert.spv"))
//                .geometryShader(resource("shadow_volume.geom.spv"))
//                .fragmentShader(resource("shadow_volume_render.frag.spv"))
//            .inputAssemblyState()
//                .trianglesWithAdjacency()
//            .rasterizationState()
//                .cullBackFace()
//                .polygonModeFill()
//            .depthStencilState()
//                .compareOpLess()
//            .colorBlendState()
//                .attachment().clear()
//                    .enableBlend()
//                    .colorBlendOp().add()
//                    .alphaBlendOp().add()
//                    .srcColorBlendFactor().srcAlpha()
//                    .dstColorBlendFactor().oneMinusSrcAlpha()
//                    .srcAlphaBlendFactor().one()
//                    .dstAlphaBlendFactor().one()
//                .add()
//            .renderPass(*m_renderPass)
//            .name("shadow_volume_debug")
//            .subpass(0)
//        .build(shadow_volume_visual.layout);
//}
//
//void ShadowVolumeGenerator::resize(VulkanRenderPass &renderPass, uint32_t width, uint32_t height) {
//    m_renderPass = &renderPass;
//    m_width = width;
//    m_height = height;
//    createFramebufferAttachments();
//    createFrameBuffer();
//    updateDescriptorSet();
//    createPipelines();
//}
//
//void ShadowVolumeGenerator::generateAdjacency(const VulkanBuffer &sourceBuffer, int triangleCount) {
//
//}
//
//void ShadowVolumeGenerator::initSamplers() {
//    VkSamplerCreateInfo samplerInfo{};
//    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
//    samplerInfo.magFilter = VK_FILTER_NEAREST;
//    samplerInfo.minFilter = VK_FILTER_NEAREST;
//    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
//    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
//    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
//    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
//    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
//
//    valueSampler = device().createSampler(samplerInfo);
//}