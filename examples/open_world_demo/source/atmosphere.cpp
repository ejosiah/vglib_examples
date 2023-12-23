//#include "atmosphere.hpp"
//#include "GraphicsPipelineBuilder.hpp"
//#include "atmosphere/AtmosphereGenerator.hpp"
//
//
//#include <utility>
//
//AtmosphereG::AtmosphereG(const VulkanDevice& device, const VulkanDescriptorPool& descriptorPool, const FileManager& fileManager,
//                       VulkanRenderPass& renderPass, uint32_t width, uint32_t height,
//                       std::shared_ptr<AtmosphereLookupTable> atmosphereLUT, std::shared_ptr<SceneGBuffer> terrainGBuffer,
//                       std::shared_ptr<ShadowVolume> terrainShadowVolume)
//        :m_device{&device}
//        ,m_descriptorPool{&descriptorPool}
//        , m_filemanager(&fileManager)
//        , m_width{width}
//        , m_height{ height }
//        , m_renderPass{ &renderPass }
//        , m_atmosphereLUT{ std::move(atmosphereLUT) }
//        , m_terrainGBuffer{ std::move(terrainGBuffer) }
//        , m_terrainShadowVolume{ std::move(terrainShadowVolume) }
//{
//    initUbo();
//    initBuffers();
//    createDescriptorSetLayouts();
//    updateDescriptorSets();
//    createPipelines();
//}
//
//
//void AtmosphereG::initBuffers() {
//    auto positions = ClipSpace::Quad::positions;
//    screenBuffer = device().createDeviceLocalBuffer(positions.data(), BYTE_SIZE(positions), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
//}
//
//void AtmosphereG::initUbo() {
//    uboBuffer = device().createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(Ubo), "atmosphere_uniforms");
//    ubo = reinterpret_cast<Ubo*>(uboBuffer.map());
//
//    ubo->white_point = glm::vec3(1);
//    ubo->earth_center = EARTH_CENTER/kLengthUnitInMeters;
//    ubo->sun_size = glm::vec3(glm::tan(kSunAngularRadius), glm::cos(kSunAngularRadius), 0);
//    ubo->exposure = 10.f;
//    ubo->lightShaft = 0;
//}
//
//void AtmosphereG::createDescriptorSetLayouts() {
//    uboSetLayout =
//        device().descriptorSetLayoutBuilder()
//            .name("ubo")
//            .binding(0)
//                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
//                .descriptorCount(1)
//                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
//        .createLayout();
//}
//
//void AtmosphereG::updateDescriptorSets() {
//    uboSet = m_descriptorPool->allocate({ uboSetLayout }).front();
//
//    auto writes = initializers::writeDescriptorSets<1>();
//
//    writes[0].dstSet = uboSet;
//    writes[0].dstBinding = 0;
//    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
//    writes[0].descriptorCount = 1;
//    VkDescriptorBufferInfo uboInfo{ uboBuffer, 0, VK_WHOLE_SIZE };
//    writes[0].pBufferInfo = &uboInfo;
//
//
//    device().updateDescriptorSets(writes);
//}
//
//void AtmosphereG::createPipelines() {
//    //    @formatter:off
//    auto builder = device().graphicsPipelineBuilder();
//    atmosphere.pipeline =
//        builder
//            .shaderStage()
//                .vertexShader(resource("atmosphere.vert.spv"))
//                .fragmentShader(resource("atmosphere.frag.spv"))
//            .vertexInputState()
//                .addVertexBindingDescriptions(ClipSpace::bindingDescription())
//                .addVertexAttributeDescriptions(ClipSpace::attributeDescriptions())
//            .inputAssemblyState()
//                .triangleStrip()
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
//                .multisampleState()
//                    .rasterizationSamples(VK_SAMPLE_COUNT_1_BIT)
//                .depthStencilState()
//                    .enableDepthWrite()
//                    .enableDepthTest()
//                    .compareOpLess()
//                    .minDepthBounds(0)
//                    .maxDepthBounds(1)
//                .colorBlendState()
//                    .attachment()
//                    .add()
//                .layout()
//                    .addDescriptorSetLayout(m_atmosphereLUT->descriptorSetLayout)
//                    .addDescriptorSetLayout(uboSetLayout)
//                    .addDescriptorSetLayout(m_terrainGBuffer->descriptorSetLayout)
//                    .addDescriptorSetLayout(m_terrainShadowVolume->setLayout)
//                .renderPass(*m_renderPass)
//                .subpass(0)
//                .name("render")
//            .build(atmosphere.layout);
//    //    @formatter:on
//}
//
//std::string AtmosphereG::resource(const std::string &name) {
//    auto res = m_filemanager->getFullPath(name);
//    assert(res.has_value());
//    return res->string();
//}
//
//void AtmosphereG::update(const SceneData &sceneData) {
//    const auto& camera = sceneData.camera;
//    ubo->model_from_view = glm::inverse(camera.view * camera.model);
//    ubo->view_from_clip = glm::inverse(camera.proj);
//    ubo->sun_direction = glm::normalize(sceneData.sun.position);
//    ubo->camera = sceneData.eyes/kLengthUnitInMeters;
//    ubo->exposure = sceneData.exposure;
//    ubo->lightShaft = static_cast<int>(sceneData.enableLightShaft);
//}
//
//void AtmosphereG::render(VkCommandBuffer commandBuffer) {
//    VkDeviceSize offset = 0;
//    static std::array<VkDescriptorSet,4> sets;
//    sets[0] = m_atmosphereLUT->descriptorSet;
//    sets[1] = uboSet;
//    sets[2] = m_terrainGBuffer->descriptorSet;
//    sets[3] = m_terrainShadowVolume->descriptorSet;
//
//    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, atmosphere.pipeline.handle);
//    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, atmosphere.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
//    vkCmdBindVertexBuffers(commandBuffer, 0, 1, screenBuffer, &offset);
//    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
//}
//
//void AtmosphereG::resize(VulkanRenderPass& renderPass, std::shared_ptr<SceneGBuffer> terrainGBuffer,
//                        std::shared_ptr<ShadowVolume> terrainShadowVolume, uint32_t width, uint32_t height) {
//    m_renderPass = &renderPass;
//    m_width = width;
//    m_height = height;
//    m_terrainGBuffer = std::move(terrainGBuffer);
//    m_terrainShadowVolume = std::move(terrainShadowVolume);
//    updateDescriptorSets();
//    createPipelines();
//}
