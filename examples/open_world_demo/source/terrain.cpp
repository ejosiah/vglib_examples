#include "Vertex.h"
#include "terrain.hpp"
#include <spdlog/spdlog.h>
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "VulkanInitializers.h"
#include <imgui.h>

Terrain::Terrain(const VulkanDevice &device, const VulkanDescriptorPool &descriptorPool, const FileManager& fileManager,
                 uint32_t width, uint32_t height, VulkanRenderPass& renderPass, std::shared_ptr<SceneGBuffer> gBuffer)
    :m_device{&device}
    ,m_descriptorPool{&descriptorPool}
    , m_filemanager(&fileManager)
    , m_width{width}
    , m_height{ height }
    , m_renderPass{ &renderPass }
    , gBuffer{std::move(gBuffer)}
{

    loadHeightMap();
    loadShadingTextures();
    initSamplers();
    initUBO();
    createGBufferFrameBuffer();
    createPatches();
    createDescriptorSetLayout();
    updateDescriptorSet();
    createPipelines();
}

void Terrain::loadHeightMap() {
    std::string terrainPath = "height_map";
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    heightMap.displacement.sampler = device().createSampler(samplerInfo);
    heightMap.normal.sampler = device().createSampler(samplerInfo);
    textures::fromFile(device(), heightMap.displacement, resource(fmt::format("terrain/{}.png", terrainPath)));
    textures::fromFile(device(), heightMap.normal, resource(fmt::format("terrain/{}_normal.png", terrainPath)));

    textures::fromFile(device(), randomTexture, resource("random.png"));
}

void Terrain::loadShadingTextures() {
    std::vector<std::string> paths{{
        resource("ground_green_grass/GroundGrassGreen004_COL_4K.jpg"),
         resource("ground_dirt_012/COL_4K.jpg"),
         resource("ground_dirt_rocky/COL_4K.jpg"),
        resource("ground_snow/GroundSnowFresh001_COL_4K.jpg")
    }};
    textures::fromFile(device(), shadingMap.albedo, paths);

    paths.clear();
    paths = {
        resource("black.png"),
        resource("black.png"),
        resource("black.png"),
        resource("black.png"),
    };
    textures::fromFile(device(), shadingMap.metalness, paths);

    paths.clear();
    paths = {
        resource("ground_green_grass/GroundGrassGreen004_GLOSS_4K.jpg"),
        resource("ground_dirt_012/GLOSS_4K.jpg"),
        resource("ground_dirt_rocky/GLOSS_4K.jpg"),
        resource("ground_snow/GroundSnowFresh001_GLOSS_4K.jpg")

    };
    textures::fromFile(device(), shadingMap.roughness, paths);

    paths.clear();
    paths = {
        resource("ground_green_grass/GroundGrassGreen004_NRM_4K.jpg"),
        resource("ground_dirt_012/NRM_4K.jpg"),
        resource("ground_dirt_rocky/NRM_4K.jpg"),
        resource("ground_snow/GroundSnowFresh001_NRM_4K.jpg")
    };
    textures::fromFile(device(), shadingMap.normal, paths);

    paths.clear();
    paths = {
        resource("ground_green_grass/GroundGrassGreen004_AO_4K.jpg"),
        resource("ground_dirt_012/AO_4K.jpg"),
        resource("ground_dirt_rocky/AO_4K.jpg"),
        resource("ground_snow/GroundSnowFresh001_AO_4K.jpg")
    };
    textures::fromFile(device(), shadingMap.ambientOcclusion, paths);

    paths.clear();
    paths = {
        resource("ground_green_grass/GroundGrassGreen004_DISP_4K.jpg"),
        resource("ground_dirt_012/DISP_4K.jpg"),
        resource("ground_dirt_rocky/DISP_4K.jpg"),
        resource("ground_snow/GroundSnowFresh001_DISP_4K.jpg")
    };
    textures::fromFile(device(), shadingMap.displacement, paths);

    textures::fromFile(device(), shadingMap.groundMask, resource("ground_mask.png"));
}

std::vector<glm::vec3> Terrain::generateNormals() {
    VulkanBuffer minMaxBuffer = device().createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(int) * 2);
    glm::ivec2 resolution(heightMap.displacement.width, heightMap.displacement.height);

    auto descriptorSetLayout =
            device().descriptorSetLayoutBuilder()
                .binding(0)
                    .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                .binding(1)
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();
    
    auto descriptorSet = descriptorPool().allocate( { descriptorSetLayout }).front();

    auto writes = initializers::writeDescriptorSets<2>();
    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo imageInfo{ heightMap.displacement.sampler.handle, heightMap.displacement.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[0].pImageInfo = &imageInfo;
    
    writes[1].dstSet = descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo bufferInfo{minMaxBuffer, 0, VK_WHOLE_SIZE};
    writes[1].pBufferInfo = &bufferInfo;
    device().updateDescriptorSets(writes);

    auto module = device().createShaderModule(resource("stats.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    auto layout = device().createPipelineLayout( {descriptorSetLayout}, {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(resolution)}} );

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = layout.handle;

    auto pipeline = device().createComputePipeline(computeCreateInfo);

    device().graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout.handle, 0, 1, &descriptorSet, 0, VK_NULL_HANDLE);
        vkCmdPushConstants(commandBuffer, layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(resolution), &resolution);
        auto groupX = static_cast<uint32_t>(glm::ceil(float(resolution.x)/32.f));
        auto groupY = static_cast<uint32_t>(glm::ceil(float(resolution.y)/32.f));

        vkCmdDispatch(commandBuffer, groupX, groupY, 1);
    });

    int* minMax = reinterpret_cast<int*>(minMaxBuffer.map());
    spdlog::info("height map range [{}, {}]", glm::intBitsToFloat(minMax[0]), glm::intBitsToFloat(minMax[1]));

    VkDeviceSize size = (SQRT_NUM_PATCHES + 1) * (SQRT_NUM_PATCHES + 1) * sizeof(glm::vec4);

    auto n = size / sizeof(glm::vec4);
    std::vector<glm::vec3> normals(n);
    normals.reserve(n);


    return normals;
}

void Terrain::createPatches() {
    float aspectRatio = glm::round(float(heightMap.displacement.width)/float(heightMap.displacement.height));
    int w = SQRT_NUM_PATCHES;
    int h = SQRT_NUM_PATCHES/aspectRatio;

    glm::vec2 whole = {w, h};
    glm::vec2 halfPatchSize{ PATCH_SIZE * 0.5f, PATCH_SIZE * 0.5f/aspectRatio };

    auto normals = generateNormals();

    std::vector<PatchVertex> vertices;
    for(int i = 0; i <= h; i++){
        for(int j = 0; j <= w; j++){
            glm::vec2 uv{ static_cast<float>(j)/whole.x, static_cast<float>(i)/whole.y };
            glm::vec2 uvOffset = 2.f * uv - 1.f; // [0, 1] => [-1, 1]
            glm::vec3 position{uvOffset.x * halfPatchSize.x, 0, uvOffset.y * halfPatchSize.y};
            auto normal = normals[i * (SQRT_NUM_PATCHES + 1) + j];

            if(glm::any(glm::isnan(normal))){
                spdlog::info("No normal: {} at : [{}, {}]", normal, j, i);
            }

            vertices.push_back({ position, normal, uv});
//            spdlog::info("index: [{}, {}] <=> uv: {}", j, i, uv);
        }
    }
    
    patchesBuffer = device().createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    std::vector<uint32_t> indices;
    for(int j = 0; j < h; j++){
        for(int i = 0; i < w; i++){
            indices.push_back((j + 1) * (w + 1) + i);
            indices.push_back((j + 1) * (w + 1) + i + 1);
            indices.push_back(j * (w + 1) + i + 1);
            indices.push_back(j * (w + 1) + i);
        }
    }
    spdlog::info("indices {}", indices.size()/4);
    indexBuffer = device().createDeviceLocalBuffer(indices.data(), BYTE_SIZE(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    ubo->numPatches = {w, h};

    std::ofstream fout{R"(D:\Program Files\SHADERed\quad_patch_normals.dat)", std::ios::binary};
    if(fout.good()){
        std::vector<glm::vec3> patches;
        for(auto index : indices){
            patches.push_back(vertices[index].normal);
        }
        fout.write(reinterpret_cast<char*>(patches.data()), sizeof(glm::vec3) * patches.size());
        fout.close();
        spdlog::info("quad patches written to destination");
    }
}

void Terrain::initUBO() {
    uboBuffer = device().createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(UniformBufferObject), "terrain");
    ubo = reinterpret_cast<UniformBufferObject*>(uboBuffer.map());
    ubo->heightScale = 1;
    ubo->minZ = 0 * meters;
    ubo->maxZ = MAX_HEIGHT;
    ubo->wireframeColor = {1, 0, 0};
    ubo->wireframe = 0;
    ubo->wireframeWidth = 5;
    ubo->lod = 0;
    ubo->lodMinDepth = 0.5 * km;
    ubo->lodMaxDepth = 5 * km;
    ubo->minTessLevel = 2;
    ubo->maxTessLevel = 64;
    ubo->lighting = 1;
    ubo->tessLevelColor = 0;
    ubo->viewportSize = { m_width, m_height };
    ubo->lodTargetTriangleWidth = 20.f;
    ubo->lodStrategy = static_cast<int>(LodStrategy::SphereProjection);
    ubo->invertRoughness = 1;
    ubo->materialId = 0;
    ubo->greenGrass = 0;
    ubo->dirt = 1;
    ubo->dirtRock = 2;
    ubo->snowFresh = 3;
    ubo->snowStart = 0.6;
    ubo->collision = 0;

    int count = 0;
    triangleCountBuffer = device().createCpuVisibleBuffer(&count, sizeof(int), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    triangleCount = reinterpret_cast<int*>(triangleCountBuffer.map());

    VkDeviceSize capacity = sizeof(Vertex) * triangleCapacity;
    vertexBuffer = device().createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_GPU_ONLY, capacity, "triangles");

    auto positions = ClipSpace::Quad::positions;
    screenBuffer = device().createDeviceLocalBuffer(positions.data(), BYTE_SIZE(positions), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void Terrain::initSamplers() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    valueSampler = device().createSampler(samplerInfo);
}

void Terrain::createDescriptorSetLayout() {
    descriptorSetLayout =
        device().descriptorSetLayoutBuilder()
            .name("terrain")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(ALL_SHADER_STAGES)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(ALL_SHADER_STAGES)
        .createLayout();

    shadingSetLayout =
        device().descriptorSetLayoutBuilder()
            .name("terrain_shading")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(4)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(5)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(6)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();
    
    trianglesSetLayout =
        device().descriptorSetLayoutBuilder()
            .name("triangles")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_GEOMETRY_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_GEOMETRY_BIT)
            .createLayout();
}

void Terrain::updateDescriptorSet() {
    auto sets = descriptorPool().allocate( { descriptorSetLayout, shadingSetLayout, trianglesSetLayout });
    
    descriptorSet = sets[0];
    shadingSet = sets[1];
    trianglesSet = sets[2];

    auto writes = initializers::writeDescriptorSets<4>();
    
    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uboInfo{ uboBuffer, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &uboInfo;

    writes[1].dstSet = descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo dispInfo{heightMap.displacement.sampler.handle, heightMap.displacement.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[1].pImageInfo = &dispInfo;

    writes[2].dstSet = descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo normalInfo{heightMap.normal.sampler.handle, heightMap.normal.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[2].pImageInfo = &normalInfo;


    writes[3].dstSet = descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo randomInfo{randomTexture.sampler.handle, randomTexture.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[3].pImageInfo = &randomInfo;

    device().updateDescriptorSets(writes);


    // update shading descriptor set
    writes = initializers::writeDescriptorSets<7>();

    writes[0].dstSet = shadingSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo albedoInfo{shadingMap.albedo.sampler.handle, shadingMap.albedo.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[0].pImageInfo = &albedoInfo;

    writes[1].dstSet = shadingSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo metalInfo{shadingMap.metalness.sampler.handle, shadingMap.metalness.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[1].pImageInfo = &metalInfo;

    writes[2].dstSet = shadingSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo roughnessInfo{shadingMap.roughness.sampler.handle, shadingMap.roughness.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[2].pImageInfo = &roughnessInfo;

    writes[3].dstSet = shadingSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo shadingNormalInfo{shadingMap.normal.sampler.handle, shadingMap.normal.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[3].pImageInfo = &shadingNormalInfo;

    writes[4].dstSet = shadingSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].descriptorCount = 1;
    VkDescriptorImageInfo aoInfo{shadingMap.ambientOcclusion.sampler.handle, shadingMap.ambientOcclusion.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[4].pImageInfo = &aoInfo;

    writes[5].dstSet = shadingSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[5].descriptorCount = 1;
    VkDescriptorImageInfo displacementInfo{shadingMap.displacement.sampler.handle, shadingMap.displacement.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[5].pImageInfo = &displacementInfo;

    writes[6].dstSet = shadingSet;
    writes[6].dstBinding = 6;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[6].descriptorCount = 1;
    VkDescriptorImageInfo groundMaskInfo{shadingMap.groundMask.sampler.handle, shadingMap.groundMask.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[6].pImageInfo = &groundMaskInfo;

    device().updateDescriptorSets(writes);

    writes = initializers::writeDescriptorSets<2>();
    
    writes[0].dstSet = trianglesSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo triCountInfo{triangleCountBuffer, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &triCountInfo;

    writes[1].dstSet = trianglesSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo triVertexInfo{vertexBuffer, 0, VK_WHOLE_SIZE};
    writes[1].pBufferInfo = &triVertexInfo;

    device().updateDescriptorSets(writes);
}

void Terrain::createPipelines() {
    //    @formatter:off
    auto builder = device().graphicsPipelineBuilder();
    terrain.pipeline =
        builder
            .shaderStage()
                .vertexShader(resource("terrain.vert.spv"))
                .tessellationControlShader(resource("terrain.tesc.spv"))
                .tessellationEvaluationShader(resource("terrain.tese.spv"))
                .geometryShader(resource("terrain.geom.spv"))
                .fragmentShader(resource("terrain.frag.spv"))
            .vertexInputState()
                .addVertexBindingDescription(0, sizeof(PatchVertex), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
                .addVertexAttributeDescription(1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3))
                .addVertexAttributeDescription(2, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec3) * 2)
            .inputAssemblyState()
                .patches()
            .tessellationState()
                .patchControlPoints(4)
                .domainOrigin(VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT)
            .viewportState()
                .viewport()
                    .origin(0, 0)
                    .dimension(m_width, m_height)
                    .minDepth(0)
                    .maxDepth(1)
                .scissor()
                    .offset(0, 0)
                    .extent(m_width, m_height)
                .add()
                .rasterizationState()
                    .cullBackFace()
                    .frontFaceCounterClockwise()
                    .polygonModeFill()
                .multisampleState()
                    .rasterizationSamples(VK_SAMPLE_COUNT_1_BIT)
                .depthStencilState()
                    .enableDepthWrite()
                    .enableDepthTest()
                    .compareOpLess()
                    .minDepthBounds(0)
                    .maxDepthBounds(1)
                .colorBlendState()
                    .attachments(5)
                .layout()
                    .addDescriptorSetLayout(descriptorSetLayout)
                    .addDescriptorSetLayout(shadingSetLayout)
                    .addDescriptorSetLayout(trianglesSetLayout)
                .renderPass(m_gBufferRenderPass)
                .subpass(0)
                .name("terrain")
            .build(terrain.layout);

    terrainDebug.pipeline =
        builder
            .shaderStage()
                .fragmentShader(resource("terrain.debug.frag.spv"))
            .colorBlendState()
                .attachments(1)
            .renderPass(*m_renderPass)
            .name("terrain_debug")
        .build(terrainDebug.layout);

    screen.pipeline =
        builder
            .shaderStage().clear()
                .vertexShader(resource("screen.vert.spv"))
                .fragmentShader(resource("terrain_render.frag.spv"))
            .vertexInputState().clear()
                .addVertexBindingDescriptions(ClipSpace::bindingDescription())
                .addVertexAttributeDescriptions(ClipSpace::attributeDescriptions())
            .inputAssemblyState()
                .triangleStrip()
            .colorBlendState()
                .attachments(1)
            .layout().clear()
                .addDescriptorSetLayout(descriptorSetLayout)
                .addDescriptorSetLayout(gBuffer->descriptorSetLayout)
            .renderPass(*m_renderPass)
            .name("terrain_render")
        .build(screen.layout);
//        @formatter:on
}


void Terrain::resize(VulkanRenderPass& renderPass, uint32_t width, uint32_t height) {
    m_renderPass = &renderPass;
    m_width = width;
    m_height = height;
    createGBufferFrameBuffer();
    updateDescriptorSet();
    createPipelines();
}

void Terrain::render(VkCommandBuffer commandBuffer) {
    if(debugMode){
        static std::array<VkDescriptorSet,3> sets;
        sets[0] = descriptorSet;
        sets[1] = shadingSet;
        sets[2] = trianglesSet;
        VkDeviceSize offset = 0;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainDebug.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainDebug.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, patchesBuffer, &offset);
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, indexBuffer.sizeAs<uint32_t>(), 1, 0, 0, 0);
    }else{
        VkDeviceSize offset = 0;
        static std::array<VkDescriptorSet,2> sets;
        sets[0] = descriptorSet;
        sets[1] = gBuffer->descriptorSet;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, screen.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, screen.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, screenBuffer, &offset);
        vkCmdDraw(commandBuffer, 4, 1, 0, 0);
    }

}

void Terrain::update(const SceneData& sceneData) {
    const auto& camera = sceneData.camera;
    ubo->model = camera.model;
    ubo->view = camera.view;
    ubo->projection = camera.proj;
    ubo->mvp = camera.proj * camera.view * camera.model;
    ubo->sunPosition = sceneData.sun.position;
    ubo->cameraPosition = sceneData.eyes;
    ubo->cameraVelocity = sceneData.cameraVelocity;
    ubo->time = sceneData.time;

    if(ubo->collision == 1){
        spdlog::info("collision point {}", ubo->collisionPoint);
    }
}

std::string Terrain::resource(const std::string &name) {
    auto res = m_filemanager->getFullPath(name);
    assert(res.has_value());
    return res->string();
}

void Terrain::renderUI() {
    ImGui::Begin("Terrain");
    ImGui::SetWindowSize({0, 0});

    static bool lighting = static_cast<bool>(ubo->lighting);
    ImGui::Checkbox("shade", &lighting);
    ubo->lighting = static_cast<int>(lighting);

    ImGui::SameLine();
    static bool wireframe = static_cast<bool>(ubo->wireframe);
    ImGui::Checkbox("wireframe", &wireframe);
    ubo->wireframe = static_cast<int>(wireframe);

    if(wireframe && ubo->tessLevelColor == 0){
        ImGui::SameLine();
        ImGui::ColorEdit3("color", &ubo->wireframeColor.x);
    }

    static bool disableHeightMap = false;
    ImGui::Checkbox("disable height map", &disableHeightMap);
    ubo->heightScale = disableHeightMap ? 0 : 1;

    static bool lod = static_cast<bool>(ubo->lod);
    ImGui::Checkbox("Dynamic LOD", &lod);
    ubo->lod = static_cast<int>(lod);

    if(lod){
        ImGui::SameLine();
        static bool tessLevelColor = static_cast<bool>(ubo->tessLevelColor);
        ImGui::Checkbox("tess level color", &tessLevelColor);
        ubo->tessLevelColor = static_cast<int>(tessLevelColor);

        if(ImGui::CollapsingHeader("Level of Detail", &lod,  ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::RadioButton("Distance from camera", &ubo->lodStrategy, static_cast<int>(LodStrategy::DistanceFromCamera));
            ImGui::SameLine();
            ImGui::RadioButton("Sphere projection", &ubo->lodStrategy, static_cast<int>(LodStrategy::SphereProjection));

            if(ubo->lodStrategy == static_cast<int>(LodStrategy::SphereProjection)) {
                ImGui::SliderFloat("target triangle width", &ubo->lodTargetTriangleWidth, 20, 1000);
            }

        }
    }

    if(lighting){
//        if(ImGui::Button("cycle material")){
//            ubo->materialId += 1;
//            ubo->materialId %= shadingMap.albedo.layers;
//            spdlog::info("material id: {}", ubo->materialId);
//        }

        static float snow = 1 - ubo->snowStart;
        ImGui::SliderFloat("snow", &snow, 0, 1);
        ubo->snowStart = 1 - snow;
    }
    ImGui::Checkbox("debug mode", &debugMode);
    ImGui::Text("triangle count: %d", *triangleCount);
    ImGui::End();
    *triangleCount = 0;
}


void Terrain::createGBufferFrameBuffer() {
    VkImageCreateInfo info = initializers::imageCreateInfo(VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
                                                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, m_width, m_height);

    VkImageSubresourceRange subresourceRange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

    depthBuffer.image = device().createImage(info);
    depthBuffer.imageView = depthBuffer.image.createView(info.format, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);

    VkAttachmentDescription attachment{
        0,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_SAMPLE_COUNT_1_BIT,
        VK_ATTACHMENT_LOAD_OP_CLEAR,
        VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_DONT_CARE,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_GENERAL
    };

    std::vector<VkAttachmentDescription> attachmentDesc;
    attachmentDesc.push_back(attachment);
    attachmentDesc.push_back(attachment);
    attachmentDesc.push_back(attachment);
    attachmentDesc.push_back(attachment);

    attachment.format = VK_FORMAT_R32_SFLOAT;
    attachmentDesc.push_back(attachment);

    attachment.format = VK_FORMAT_D32_SFLOAT;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
    attachmentDesc.push_back(attachment);

    SubpassDescription subpass{};
    subpass.colorAttachments = {
            {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {4, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
    };
    subpass.depthStencilAttachments.attachment = 5;
    subpass.depthStencilAttachments.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;

    std::vector<SubpassDescription> subpassDesc{ subpass };

    std::vector<VkSubpassDependency> dependencies{
            {
                VK_SUBPASS_EXTERNAL,
                0,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                0
            },
            {
                0,
                VK_SUBPASS_EXTERNAL,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                0
            }
    };


    m_gBufferRenderPass = device().createRenderPass(attachmentDesc, subpassDesc, dependencies);

    std::vector<VkImageView> attachments{
        gBuffer->position.imageView.handle,
        gBuffer->normal.imageView.handle,
        gBuffer->albedo.imageView.handle,
        gBuffer->material.imageView.handle,
        gBuffer->depth.imageView.handle,
        depthBuffer.imageView.handle,
    };
    m_gBufferFramebuffer = device().createFramebuffer(m_gBufferRenderPass, attachments, m_width, m_height);
}

void Terrain::renderTerrain() {
    if(debugMode) return;
    device().graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        static std::array<VkClearValue, 6> clearValues;
        clearValues[0].color = {0, 0, 0, 0};
        clearValues[1].color = {0, 0, 0, 0};
        clearValues[2].color = {0, 0, 0, 0};
        clearValues[3].color = {0, 0, 0, 0};
        clearValues[4].color = {1, 1, 1, 1};
        clearValues[5].depthStencil = {1.0, 0u};

        VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
        rPassInfo.clearValueCount = COUNT(clearValues);
        rPassInfo.pClearValues = clearValues.data();
        rPassInfo.framebuffer = m_gBufferFramebuffer;
        rPassInfo.renderArea.offset = {0u, 0u};
        rPassInfo.renderArea.extent = { m_width, m_height};
        rPassInfo.renderPass = m_gBufferRenderPass;

        vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        static std::array<VkDescriptorSet,3> sets;
        sets[0] = descriptorSet;
        sets[1] = shadingSet;
        sets[2] = trianglesSet;
        VkDeviceSize offset = 0;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrain.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrain.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, patchesBuffer, &offset);
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, indexBuffer.sizeAs<uint32_t>(), 1, 0, 0, 0);

        vkCmdEndRenderPass(commandBuffer);
    });
}

