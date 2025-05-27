#include "Collision3D.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include "Barrier.hpp"
#include <fmt/color.h>

#define THREE_DIMENSIONS 3u
#define D_BITS THREE_DIMENSIONS
#define HOME_CELL_MASK ((1u << D_BITS) - 1u)
#define INTERSECTING_CELLS_MASK ((1u << (1u << D_BITS)) - 1u)
#define HOME_CELL_TYPE(ctrlBits)  (1u << (ctrlBits & HOME_CELL_MASK))
#define CELL_TYPE_INDEX(X, Y, Z) ((X % 2u) + (Y % 2u) * 2u + (Z % 2u) * 4u)


Collision3D::Collision3D(const Settings& settings) : VulkanBaseApp("3D collision", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("collision_3d");
    fileManager().addSearchPathFront("collision_3d/data");
    fileManager().addSearchPathFront("collision_3d/spv");
    fileManager().addSearchPathFront("collision_3d/models");
    fileManager().addSearchPathFront("collision_3d/textures");

    pauseAction = &mapToKey(Key::S, "Pause sim", Action::detectInitialPressOnly());
    statusAction = &mapToKey(Key::T, "Display status", Action::detectInitialPressOnly());
}

void Collision3D::initApp() {
    if(debug.enabled){
        domain = { {0, 0, 0}, {2, 2, 2} };
    }else {
        domain = {{-4, 0, -4}, {4,  4, 4}};
    }
    createGizmo();
    createShapes();
    initScratchBuffer();
    initObjects();
    initDebug();
    initParticleEmitters();
    initSphereEmitters();
    initCamera();
    createInverseCam();
    createDescriptorPool();
    initBindlessDescriptor();
//    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    initSort();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
}

void Collision3D::initCamera() {
    OrbitingCameraSettings cameraSettings;
//    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.1;
    cameraSettings.orbitMaxZoom = 512.0f;
    cameraSettings.offsetDistance = 1.0f;
    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
}

void Collision3D::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void Collision3D::beforeDeviceCreation() {
    auto devFeatures12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    if(devFeatures12.has_value()) {
        devFeatures12.value()->scalarBlockLayout = VK_TRUE;
        devFeatures12.value()->shaderOutputViewportIndex = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan12Features devFeatures12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        devFeatures12.scalarBlockLayout = VK_TRUE;
        devFeatures12.shaderOutputViewportIndex = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, devFeatures12);
    }


    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    if(devFeatures13.has_value()) {
        devFeatures13.value()->synchronization2 = VK_TRUE;
        devFeatures13.value()->dynamicRendering = VK_TRUE;
        devFeatures13.value()->maintenance4 = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan13Features devFeatures13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        devFeatures13.synchronization2 = VK_TRUE;
        devFeatures13.dynamicRendering = VK_TRUE;
        devFeatures13.maintenance4 = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, devFeatures13);
    };

    static VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dsFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };
    dsFeatures.extendedDynamicState3PolygonMode = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, dsFeatures);

    static VkPhysicalDeviceIndexTypeUint8FeaturesEXT indexType8{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT };
    indexType8.indexTypeUint8 = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, indexType8);
}

void Collision3D::createDescriptorPool() {
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


void Collision3D::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void Collision3D::createDescriptorSetLayouts() {
    objects.setLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(2)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(4)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(5)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(6)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(7)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(8)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(9)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();

    globalSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();

    emitterSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(2)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();

    stagingSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();

    debug.descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
}

void Collision3D::updateDescriptorSets(){
    // objects
    auto sets = descriptorPool.allocate({
        globalSetLayout, objects.setLayout, emitterSetLayout, stagingSetLayout, debug.descriptorSetLayout
    });
    globalSet = sets[0];
    objects.descriptorSet = sets[1];
    emitterDescriptorSet = sets[2];
    stagingDescriptorSet = sets[3];
    debug.descriptorSet = sets[4];

    auto writes = initializers::writeDescriptorSets<16>();

    writes[0].dstSet = globalSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo globalInfo{ globals.gpu, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &globalInfo;

    // Objects

    writes[1].dstSet = objects.descriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 2;
    std::array<VkDescriptorBufferInfo, 2> positionInfo{{
        { objects.position[0], 0, VK_WHOLE_SIZE }, { objects.position[1], 0, VK_WHOLE_SIZE }
    }};
    writes[1].pBufferInfo = positionInfo.data();

    writes[2].dstSet = objects.descriptorSet;
    writes[2].dstBinding = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo velocityInfo{ objects.velocity, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &velocityInfo;

    writes[3].dstSet = objects.descriptorSet;
    writes[3].dstBinding = 2;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    VkDescriptorBufferInfo cvInfo{ objects.correctionVector, 0, VK_WHOLE_SIZE };
    writes[3].pBufferInfo = &cvInfo;

    writes[4].dstSet = objects.descriptorSet;
    writes[4].dstBinding = 3;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    VkDescriptorBufferInfo radiusInfo{ objects.radius, 0, VK_WHOLE_SIZE };
    writes[4].pBufferInfo = &radiusInfo;

    writes[5].dstSet = objects.descriptorSet;
    writes[5].dstBinding = 4;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = 1;
    VkDescriptorBufferInfo cellIdInfo{ objects.cellIds, 0, VK_WHOLE_SIZE };
    writes[5].pBufferInfo = &cellIdInfo;

    writes[6].dstSet = objects.descriptorSet;
    writes[6].dstBinding = 5;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[6].descriptorCount = 1;
    VkDescriptorBufferInfo attributesInfo{ objects.attributes, 0, VK_WHOLE_SIZE };
    writes[6].pBufferInfo = &attributesInfo;

    writes[7].dstSet = objects.descriptorSet;
    writes[7].dstBinding = 6;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[7].descriptorCount = 1;
    VkDescriptorBufferInfo countsInfo{ objects.counts, 0, VK_WHOLE_SIZE };
    writes[7].pBufferInfo = &countsInfo;

    writes[8].dstSet = objects.descriptorSet;
    writes[8].dstBinding = 7;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[8].descriptorCount = 1;
    VkDescriptorBufferInfo cellIndexInfo{ objects.cellIndexArray, 0, VK_WHOLE_SIZE };
    writes[8].pBufferInfo = &cellIndexInfo;

    writes[9].dstSet = objects.descriptorSet;
    writes[9].dstBinding = 8;
    writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[9].descriptorCount = 1;
    VkDescriptorBufferInfo dispatchCmdInfo{ objects.dispatchBuffer, 0, VK_WHOLE_SIZE };
    writes[9].pBufferInfo = &dispatchCmdInfo;

    writes[10].dstSet = objects.descriptorSet;
    writes[10].dstBinding = 9;
    writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[10].descriptorCount = 1;
    VkDescriptorBufferInfo distanceConstraint{ objects.indices, 0, VK_WHOLE_SIZE };
    writes[10].pBufferInfo = &distanceConstraint;


    // emiiter
    std::vector<VkDescriptorBufferInfo> emitterInfo {
            { emitters.particle, 0, VK_WHOLE_SIZE },
            { emitters.sphere, 0, VK_WHOLE_SIZE },
    };
    writes[11].dstSet = emitterDescriptorSet;
    writes[11].dstBinding = 0;
    writes[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[11].descriptorCount = emitterInfo.size();
    writes[11].pBufferInfo = emitterInfo.data();

    // staging
    writes[12].dstSet = stagingDescriptorSet;
    writes[12].dstBinding = 0;
    writes[12].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[12].descriptorCount = 1;
    VkDescriptorBufferInfo cellIndexStagingInfo{ *objects.cellIndexStaging.buffer, objects.cellIndexStaging.offset, objects.cellIndexStaging.end };
    writes[12].pBufferInfo = &cellIndexStagingInfo;

    writes[13].dstSet = stagingDescriptorSet;
    writes[13].dstBinding = 1;
    writes[13].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[13].descriptorCount = 1;
    VkDescriptorBufferInfo SetBitsInfo{*objects.bitSet.buffer, objects.bitSet.offset, objects.bitSet.end };
    writes[13].pBufferInfo = &SetBitsInfo;

    writes[14].dstSet = stagingDescriptorSet;
    writes[14].dstBinding = 2;
    writes[14].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[14].descriptorCount = 1;
    VkDescriptorBufferInfo compactInfo{ *objects.compactIndices.buffer, objects.compactIndices.offset, objects.compactIndices.end };
    writes[14].pBufferInfo = &compactInfo;

    writes[15].dstSet = debug.descriptorSet;
    writes[15].dstBinding = 0;
    writes[15].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[15].descriptorCount = 1;
    VkDescriptorBufferInfo debugInfo{ debug.buffer.buffer, 0, VK_WHOLE_SIZE };
    writes[15].pBufferInfo = &debugInfo;


    device.updateDescriptorSets(writes);

}

void Collision3D::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void Collision3D::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}

void Collision3D::createInverseCam() {
    inverseCamProj = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(glm::mat4) * 2);
}

void Collision3D::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.bounds.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("bounds.vert.spv"))
                    .fragmentShader(resource("bounds.frag.spv"))
                .rasterizationState()
                    .cullNone()
                .name("bounds")
            .build(render.bounds.layout);

        render.flat.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("flat.vert.spv"))
                    .fragmentShader(resource("flat.frag.spv"))
                .rasterizationState()
                    .cullNone()
                    .lineWidth(2.0)
                .dynamicState()
                    .primitiveTopology()
                    .depthCompareOp()
                .name("flat")
            .build(render.flat.layout);

        render.shape.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("particles.vert.spv"))
                    .fragmentShader(resource("render.frag.spv"))
                .rasterizationState()
                    .cullNone()
                .layout()
                    .addDescriptorSetLayout(globalSetLayout)
                    .addDescriptorSetLayout(objects.setLayout)
                .name("particles")
            .build(render.shape.layout);

        render.grid.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("flat.vert.spv"))
                    .fragmentShader(resource("grid.frag.spv"))
                .rasterizationState()
                    .cullNone()
                .dynamicState()
                    .primitiveTopology()
                .layout()
                    .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(Camera), sizeof(glm::ivec3))
                    .addDescriptorSetLayout(globalSetLayout)
                    .addDescriptorSetLayout(objects.setLayout)
                .name("grid")
            .build(render.grid.layout);


    //    @formatter:on
}

void Collision3D::createComputePipeline() {
    auto module = device.createShaderModule(resource("emitter.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute.emitter.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout, emitterSetLayout} );

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.emitter.layout.handle;

    compute.emitter.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("particle_emitter", compute.emitter.pipeline.handle);

    // sphere emitter
    module = device.createShaderModule(resource("sphere_emitter.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.sphereEmitter.layout = device.createPipelineLayout( {  globalSetLayout, objects.setLayout, emitterSetLayout} );
    computeCreateInfo.layout = compute.sphereEmitter.layout.handle;
    compute.sphereEmitter.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("sphere_emitter", compute.sphereEmitter.pipeline.handle);


    // integrator
    module = device.createShaderModule(resource("integrate.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.integrate.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout} );
    computeCreateInfo.layout = compute.integrate.layout.handle;
    compute.integrate.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("integrate", compute.integrate.pipeline.handle);

    // bounds check
    module = device.createShaderModule(resource("bounds_check.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.boundsCheck.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout} );
    computeCreateInfo.layout = compute.boundsCheck.layout.handle;
    compute.boundsCheck.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("bounds_check", compute.boundsCheck.pipeline.handle);

    // corrections
    module = device.createShaderModule(resource("apply_correction.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.correction.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout} );
    computeCreateInfo.layout = compute.correction.layout.handle;
    compute.correction.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("apply_correction", compute.correction.pipeline.handle);

    // velocity update
    module = device.createShaderModule(resource("update_velocity.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.velocity.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout} );
    computeCreateInfo.layout = compute.velocity.layout.handle;
    compute.velocity.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("update_velocity", compute.velocity.pipeline.handle);

    // constraints
    Pipeline distanceConstraint;
    module = device.createShaderModule(resource("distance_constraint_solver.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    distanceConstraint.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout} );
    computeCreateInfo.layout = distanceConstraint.layout.handle;
    distanceConstraint.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("distance_constraint_solver", distanceConstraint.pipeline.handle);
    compute.constraints.push_back(std::move(distanceConstraint));

    // compute indirect
    module = device.createShaderModule(resource("compute_dispatch.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.computeDispatch.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout}, { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) * 2 }} );
    computeCreateInfo.layout = compute.computeDispatch.layout.handle;
    compute.computeDispatch.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("compute_dispatch", compute.computeDispatch.pipeline.handle);


    // init cell ids
    module = device.createShaderModule(resource("initialize_cell_ids.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.initCellIDs.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout, debug.descriptorSetLayout}, { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) * 2 }} );
    computeCreateInfo.layout = compute.initCellIDs.layout.handle;
    compute.initCellIDs.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("initialize_cellIDs", compute.initCellIDs.pipeline.handle);

    // count cells
    module = device.createShaderModule(resource("count_cells.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.countCells.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout} );
    computeCreateInfo.layout = compute.countCells.layout.handle;
    compute.countCells.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("count_cells", compute.countCells.pipeline.handle);


    // generate cell index
    module = device.createShaderModule(resource("generate_cell_index_array.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.generateCellIndexArray.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout, stagingSetLayout} );
    computeCreateInfo.layout = compute.generateCellIndexArray.layout.handle;
    compute.generateCellIndexArray.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("generate_cell_index_array", compute.generateCellIndexArray.pipeline.handle);

    // compact cell index array
    module = device.createShaderModule(resource("compact_cell_index_array.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.compactCellIndexArray.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout, stagingSetLayout} );
    computeCreateInfo.layout = compute.compactCellIndexArray.layout.handle;
    compute.compactCellIndexArray.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("compact_cell_index_array", compute.compactCellIndexArray.pipeline.handle);

    // collision test
    module = device.createShaderModule(resource( "collision_test.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.collisionTest.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout, stagingSetLayout}, { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t)} } );
    computeCreateInfo.layout = compute.collisionTest.layout.handle;
    compute.collisionTest.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("collision_test", compute.collisionTest.pipeline.handle);

}


void Collision3D::onSwapChainDispose() {
    dispose(render.bounds.pipeline);
    dispose(render.shape.pipeline);
    dispose(compute.emitter.pipeline);
}

void Collision3D::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
    createComputePipeline();
    camera->perspective(swapChain.aspectRatio());
}

VkCommandBuffer *Collision3D::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    runSimulation(commandBuffer);

    static std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = {0.2, 0.2, 0.2, 1};
    clearValues[1].depthStencil = {1.0, 0u};

    VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
    rPassInfo.clearValueCount = COUNT(clearValues);
    rPassInfo.pClearValues = clearValues.data();
    rPassInfo.framebuffer = framebuffers[imageIndex];
    rPassInfo.renderArea.offset = {0u, 0u};
    rPassInfo.renderArea.extent = swapChain.extent;
    rPassInfo.renderPass = renderPass;

    vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    if(!debug.enabled) {
        renderBounds(commandBuffer);
    }else {
        renderGrid(commandBuffer);
    }
    renderParticles(commandBuffer);

    renderGizmo(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

//    rayTrace(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void Collision3D::renderBounds(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.bounds.pipeline.handle);
    camera->push(commandBuffer, render.bounds.layout, identity);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &bounds.vertices.buffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, bounds.indexes,  0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, bounds.indexes.sizeAs<uint32_t>(), 1, 0, 0, 0);
}

void Collision3D::renderParticles(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.shape.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.shape.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    camera->push(commandBuffer, render.shape.layout, identity);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &ball.vertices.buffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, ball.indexes,  0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, ball.indexes.sizeAs<uint32_t>(), globals.cpu->numObjects, 0, 0, 0);
}

void Collision3D::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
    globals.cpu->frame++;
    fixedUpdate.advance(time);
    setTitle(fmt::format("{}, {} active objects", title, globals.cpu->numObjects));
}

void Collision3D::checkAppInputs() {
    camera->processInput();
    if(pauseAction->isPressed()) {
        pauseRequested = true;
    }
    if(statusAction->isPressed()) {
        displayStatus = true;
    }
}

void Collision3D::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void Collision3D::onPause() {
    VulkanBaseApp::onPause();
}

void Collision3D::createShapes() {
    auto scale = (domain.upper - domain.lower) * 0.5f;
    auto transform = glm::translate(glm::mat4{1}, {0, scale.y, 0});
    transform = glm::scale(transform, scale);
    auto wall = primitives::cube(glm::vec4(0.6), transform);

    bounds.vertices = device.createDeviceLocalBuffer(wall.vertices.data(), BYTE_SIZE(wall.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    bounds.indexes = device.createDeviceLocalBuffer(wall.indices.data(), BYTE_SIZE(wall.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    auto sphere = primitives::sphere(50, 50, 1.f, glm::mat4{1}, {0, 1, 0, 1}, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    ball.vertices = device.createDeviceLocalBuffer(sphere.vertices.data(), BYTE_SIZE(sphere.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    ball.indexes = device.createDeviceLocalBuffer(sphere.indices.data(), BYTE_SIZE(sphere.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    transform = glm::scale(glm::mat4{1}, glm::vec3(0.5));
    auto cube = primitives::cube(glm::vec4(0.6), transform);
    cell.solid.vertices = device.createDeviceLocalBuffer(cube.vertices.data(), BYTE_SIZE(cube.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    cell.solid.indexes = device.createDeviceLocalBuffer(cube.indices.data(), BYTE_SIZE(cube.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    cube = primitives::cubeOutline(glm::vec4(0.6), transform);
    cell.outline.vertices = device.createDeviceLocalBuffer(cube.vertices.data(), BYTE_SIZE(cube.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void Collision3D::initObjects() {
    if(debug.enabled) return;
    globals.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(GlobalData));
    globals.cpu = reinterpret_cast<GlobalData*>(globals.gpu.map());

    globals.cpu->domain.lower = domain.lower;
    globals.cpu->domain.upper = domain.upper;
    globals.cpu->gravity = {0, -9.8f, 0};
    globals.cpu->numObjects = 0;
    globals.cpu->numDistanceConstraints = 0;
    globals.cpu->segmentSize = 2;
    globals.cpu->frame = 0;
    globals.cpu->time = fixedUpdate.period();
    globals.cpu->restitution = 0.75;

    globals.cpu->halfSpacing = objects.defaultRadius;
    const auto spacing = glm::sqrt(2.f) * objects.defaultRadius * 2;
    globals.cpu->spacing = spacing;

    globals.cpu->gridSize = gridSize();

    static constexpr VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    uint32_t numParticle = objects.maxParticles;

    objects.position[0] = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, numParticle * sizeof(glm::vec3));
    objects.position[1] = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, memoryUsage, numParticle * sizeof(glm::vec3));
    objects.correctionVector = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memoryUsage, numParticle * sizeof(glm::vec3));
    objects.velocity = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memoryUsage, numParticle * sizeof(glm::vec3));
    objects.radius = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memoryUsage, numParticle * sizeof(float));
    objects.constraints.distance = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memoryUsage, numParticle * sizeof(DistanceConstraint));


    objects.cellIds = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(uint32_t) * numParticle * 8);
    prevCellIds = device.createBuffer( VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(uint32_t) * numParticle* 8);
    objects.attributes = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(Attribute) * numParticle * 8);
    prevAttributes = device.createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(Attribute) * numParticle * 8);
    objects.counts = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, memoryUsage, sizeof(uint32_t) * (objects.gridSize + 1));
    objects.cellIndexArray = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(CellInfo) * objects.gridSize);
    objects.cellIndexStaging = reserve(sizeof(CellInfo) * objects.gridSize);
    objects.bitSet = reserve(sizeof(uint32_t) * std::max(objects.gridSize, numParticle));
    objects.compactIndices = reserve(sizeof(uint32_t) * (objects.gridSize + 1));
    objects.indices = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, numParticle * sizeof(uint32_t) * 8);


    objects.dispatchBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, memoryUsage, Dispatch::Size, "dispatch_cmd_buffer");
    debug.buffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memoryUsage, sizeof(DebugInfo) * 1, "debug_info");
}

void Collision3D::initDebug() {
    if(!debug.enabled) return;
    globals.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(GlobalData));
    globals.cpu = reinterpret_cast<GlobalData*>(globals.gpu.map());

    globals.cpu->domain.lower = domain.lower;
    globals.cpu->domain.upper = domain.upper;
    globals.cpu->gravity = {0, -9.8f, 0};
    globals.cpu->numDistanceConstraints = 0;
    globals.cpu->segmentSize = 2;
    globals.cpu->frame = 0;
    globals.cpu->time = fixedUpdate.period();

    const auto scale = 1.0f;
    auto defaultRadius = 0.35355339059327376220042218105242f * scale;
    globals.cpu->halfSpacing = defaultRadius;
    auto spacing = 1.f * scale;
    globals.cpu->spacing = spacing;


    globals.cpu->gridSize = gridSize();

    static constexpr VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    uint32_t numParticle = objects.maxParticles;

    objects.position[0] = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, numParticle * sizeof(glm::vec3));
    objects.position[1] = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, memoryUsage, numParticle * sizeof(glm::vec3));
    objects.correctionVector = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memoryUsage, numParticle * sizeof(glm::vec3));
    objects.velocity = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memoryUsage, numParticle * sizeof(glm::vec3));
    objects.radius = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memoryUsage, numParticle * sizeof(float));
    objects.constraints.distance = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memoryUsage, numParticle * sizeof(DistanceConstraint));


    objects.cellIds = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(uint32_t) * numParticle * 8);
    prevCellIds = device.createBuffer( VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(uint32_t) * numParticle* 8);
    objects.attributes = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(Attribute) * numParticle * 8);
    prevAttributes = device.createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(Attribute) * numParticle * 8);
    objects.counts = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, memoryUsage, sizeof(uint32_t) * (objects.gridSize + 1));
    objects.cellIndexArray = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(CellInfo) * objects.gridSize);
    objects.cellIndexStaging = reserve(sizeof(CellInfo) * objects.gridSize);
    objects.bitSet = reserve(sizeof(uint32_t) * std::max(objects.gridSize, numParticle));
    objects.compactIndices = reserve(sizeof(uint32_t) * (objects.gridSize + 1));
    objects.indices = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, numParticle * sizeof(uint32_t) * 8);

    objects.dispatchBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, memoryUsage, Dispatch::Size, "dispatch_cmd_buffer"); ;

    debug.positions[0] = objects.position[0].span<glm::vec3>();
    debug.positions[1] = objects.position[1].span<glm::vec3>();
    debug.radius = objects.radius.span<float>();
    debug.counts = objects.counts.span<uint32_t>();
//    debug.positions[1][0] = glm::vec3{0.02, 0.02, 0};
//    debug.positions[1][0] = glm::vec3{-0.2, -0.5, -0.5};
//    debug.positions[1][0] = glm::vec3{1.02, 1.02, 1};
//    debug.positions[1][0] = glm::vec3{0.8, 0.5, 0.5};

    debug.positions[1][0] = glm::vec3{0.5, 0.5, 0.5};
    debug.positions[1][1] = glm::vec3{1.0, 0.5, 0.5};

//    debug.positions[1][1] = glm::vec3{1.5, 0.5, 0.5};
//    debug.positions[1][0] = glm::vec3{0.5, 0.5, 0.5};
//    debug.positions[1][2] = glm::vec3{0.5, 1.5, 0.5};
//    debug.positions[1][3] = glm::vec3{1.5, 1.5, 0.5};
//
//    debug.positions[1][4] = glm::vec3{0.5, 0.5, 1.5};
//    debug.positions[1][5] = glm::vec3{1.5, 0.5, 1.5};
//    debug.positions[1][6] = glm::vec3{0.5, 1.5, 1.5};
//    debug.positions[1][7] = glm::vec3{1.5, 1.5, 1.5};

    globals.cpu->numObjects = 2;
    std::fill_n(debug.radius.begin(), globals.cpu->numObjects, defaultRadius);
    debug.buffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memoryUsage, sizeof(DebugInfo) * 1000, "debug_info");
    debug.info = debug.buffer.span<DebugInfo>();
}

BufferRegion Collision3D::reserve(VkDeviceSize size) {
    size = alignedSize(size, device.getLimits().minStorageBufferOffsetAlignment);
    assert(scratchPad.offset + size <= scratchPad.buffer.size);
    auto start = scratchPad.offset;
    scratchPad.offset += size;
    return { &scratchPad.buffer, start, scratchPad.offset };
}

void Collision3D::initScratchBuffer() {
    scratchPad.buffer = device.createBuffer(
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_GPU_TO_CPU, (20 * (1 << 20)), "scratch_buffer");
}

void Collision3D::initParticleEmitters() {
    const auto& domain = globals.cpu->domain;
    const auto radius = objects.defaultRadius;
    globals.cpu->numEmitters = 10;

    Emitter prototype{
        .origin = { 0, 5, 3.5 },
        .direction = {0, -1, -1},
        .radius = radius,
        .offset = radius * 4,
        .speed = 2,
        .spreadAngleRad = 1,
        .maxNumberOfParticlePerSecond = 25,
        .maxNumberOfParticles = static_cast<int>(glm::max(1u, objects.maxParticles / globals.cpu->numEmitters)),
        .firstFrameTimeInSeconds = 0,
        .currentTime = 0,
        .numberOfEmittedParticles = 0,
        .disabled = false,
    };

    std::vector<Emitter> emits{};
    emits.push_back(prototype);

    auto r = radius * 3.f;
    for(auto i = 1; i <= 3; ++i) {
        auto angle = glm::two_pi<float>() * to<float>(i)/3.f;
        Emitter emitter = prototype;
        emitter.origin = prototype.origin + r * glm::vec3(glm::sin(angle), 0, glm::cos(angle));
        emits.push_back(emitter);
    }

    r *= 2.f;
    for(auto i = 1; i <= 6; ++i) {
        auto angle = glm::two_pi<float>() * to<float>(i)/6.f;
        Emitter emitter = prototype;
        emitter.origin = prototype.origin + r * glm::vec3(glm::sin(angle), 0, glm::cos(angle));
        emits.push_back(emitter);
    }

    emitters.particle = device.createDeviceLocalBuffer(emits.data(), BYTE_SIZE(emits), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

void Collision3D::initSphereEmitters() {
    const auto& domain = globals.cpu->domain;
    const auto radius = objects.defaultRadius;
    const auto diameter = radius * 2;
    globals.cpu->numSphereEmitters = 1;

    Emitter prototype{
        .origin = { 0, 5, 3.5 },
        .direction = {0, -1, -1},
        .radius = radius,
        .offset = 1.25,
        .speed = 2,
        .spreadAngleRad = 0,
        .maxNumberOfParticlePerSecond = 10,
        .maxNumberOfParticles = static_cast<int>(glm::max(1u, objects.maxParticles / globals.cpu->numEmitters)),
        .firstFrameTimeInSeconds = 0,
        .currentTime = 0,
        .numberOfEmittedParticles = 0,
        .disabled = false,
    };

    std::vector<Emitter> emits{};
    for(auto i = 0; i < globals.cpu->numSphereEmitters; ++i){
        Emitter emitter = prototype;
//        emitter.origin.x = domain.upper.x - 2.f * (radius + radius * static_cast<float>(i));
        emits.push_back(emitter);
    }
    emitters.sphere = device.createDeviceLocalBuffer(emits.data(), BYTE_SIZE(emits), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

void Collision3D::runSimulation(VkCommandBuffer commandBuffer) {
    if(pauseSim || debug.enabled) return;

    static int count = 0;
    if(count > 0) return;
//    count++;

    fixedUpdate([&]{
        Barrier::fragmentReadToComputeWrite(commandBuffer);
        emitParticles(commandBuffer);
        computeDispatch(commandBuffer, Dispatch::Object);
        checkBounds(commandBuffer);
        processCollisions(commandBuffer);
        integrate(commandBuffer);
        Barrier::computeWriteToFragmentRead(commandBuffer);
    });
}

void Collision3D::emitParticles(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    sets[2] = emitterDescriptorSet;

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.emitter.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.emitter.pipeline.handle);
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);

//    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.sphereEmitter.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
//    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.sphereEmitter.pipeline.handle);
//    vkCmdDispatch(commandBuffer, 1, 1, 1);
//    Barrier::computeWriteToRead(commandBuffer);
}

void Collision3D::integrate(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.integrate.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.integrate.pipeline.handle);
    vkCmdDispatchIndirect(commandBuffer, objects.dispatchBuffer, Dispatch::ObjectCmd);
    Barrier::computeWriteToRead(commandBuffer);
}

void Collision3D::solveConstraints(VkCommandBuffer commandBuffer) {
//    for(auto& constraint : compute.constraints) {
//        solveConstraint(constraint, commandBuffer);
//        applyCorrection(commandBuffer);
//    }
}

void Collision3D::solveConstraint(Pipeline &pipeline, VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline.handle);
    vkCmdDispatch(commandBuffer, globals.cpu->numObjects, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void Collision3D::applyCorrection(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.correction.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.correction.pipeline.handle);
    vkCmdDispatch(commandBuffer, globals.cpu->numObjects, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void Collision3D::updateVelocity(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.velocity.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.velocity.pipeline.handle);
    vkCmdDispatchIndirect(commandBuffer, objects.dispatchBuffer, Dispatch::ObjectCmd);
    Barrier::computeWriteToRead(commandBuffer);
}

void Collision3D::checkBounds(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.boundsCheck.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.boundsCheck.pipeline.handle);
    vkCmdDispatchIndirect(commandBuffer, objects.dispatchBuffer, Dispatch::ObjectCmd);
    Barrier::computeWriteToRead(commandBuffer);
}

void Collision3D::processCollisions(VkCommandBuffer commandBuffer) {
    computeDispatch(commandBuffer, Dispatch::Object);
    initializeCellIds(commandBuffer);
    sortCellIds(commandBuffer);
    computeDispatch(commandBuffer, Dispatch::CellID);
    countCells(commandBuffer);
    generateCellIndexArray(commandBuffer);
    compactCellIndexArray(commandBuffer);
    computeDispatch(commandBuffer, Dispatch::CellArrayIndex);
    resolveCollision(commandBuffer);
}

void Collision3D::computeDispatch(VkCommandBuffer commandBuffer, uint32_t objectType) {
    static std::array<uint32_t, 2> constants{};
    constants[0] = workGroupSize;
    constants[1] = objectType;

    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.computeDispatch.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.computeDispatch.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, compute.computeDispatch.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, BYTE_SIZE(constants), constants.data());
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    Barrier::computeWriteToDrawIndirect(commandBuffer);
}

void Collision3D::initializeCellIds(VkCommandBuffer commandBuffer) {
    globals.cpu->numCells = 0;
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    sets[2] = debug.descriptorSet;

    vkCmdFillBuffer(commandBuffer, objects.cellIds, 0, objects.cellIds.size, 0xFFFFFFFF);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.initCellIDs.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.initCellIDs.pipeline.handle);
    vkCmdDispatchIndirect(commandBuffer, objects.dispatchBuffer, Dispatch::Object);

    VkBufferCopy region{0, 0, objects.cellIds.size};
    Barrier::computeWriteToTransferRead(commandBuffer);
    vkCmdCopyBuffer(commandBuffer, objects.cellIds, prevCellIds, 1, &region);
    region.size = objects.attributes.size;
    vkCmdCopyBuffer(commandBuffer, objects.attributes, prevAttributes, 1, &region);
    Barrier::computeWriteToRead(commandBuffer);
}

void Collision3D::sortCellIds(VkCommandBuffer commandBuffer) {
    Records records{objects.attributes, 8};
    sort.sortWithIndices(commandBuffer, objects.cellIds, objects.indices);
    Barrier::computeWriteToRead(commandBuffer);
}

void Collision3D::initSort() {
    sort = RadixSort{ &device };
    sort.init();
    sort.enableOrderChecking();
    prefixSum = PrefixSum{ &device };
    prefixSum.init();
}

void Collision3D::countCells(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;


    Barrier::computeWriteToTransferRead(commandBuffer);
    vkCmdFillBuffer(commandBuffer, objects.counts, 0, objects.counts.size, 0);
    Barrier::transferWriteToComputeRead(commandBuffer);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.countCells.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.countCells.pipeline.handle);
    vkCmdDispatchIndirect(commandBuffer, objects.dispatchBuffer, Dispatch::CellIDCmd);

    Barrier::computeWriteToRead(commandBuffer);
    prefixSum(commandBuffer, objects.counts);
    Barrier::computeWriteToRead(commandBuffer);
}

void Collision3D::generateCellIndexArray(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    sets[2] = stagingDescriptorSet;

    uint32_t gx = objects.gridSize/workGroupSize + glm::sign(static_cast<float>(objects.gridSize % workGroupSize));


    vkCmdFillBuffer(commandBuffer, *objects.bitSet.buffer, objects.bitSet.offset, objects.bitSet.size(), 0);
    vkCmdFillBuffer(commandBuffer, *objects.compactIndices.buffer, objects.compactIndices.offset, objects.compactIndices.size(), 0);
    vkCmdFillBuffer(commandBuffer, *objects.cellIndexStaging.buffer, objects.cellIndexStaging.offset, objects.cellIndexStaging.size(), 0);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.generateCellIndexArray.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.generateCellIndexArray.pipeline.handle);
    vkCmdDispatch(commandBuffer, gx, 1, 1);

    Barrier::computeWriteToTransferRead(commandBuffer);
    VkBufferCopy region{objects.bitSet.offset, objects.compactIndices.offset, objects.bitSet.size() };
    vkCmdCopyBuffer(commandBuffer, *objects.bitSet.buffer, *objects.compactIndices.buffer, 1, &region);
    Barrier::transferWriteToComputeRead(commandBuffer);
}

void Collision3D::compactCellIndexArray(VkCommandBuffer commandBuffer) {
    prefixSum(commandBuffer, objects.compactIndices);
    Barrier::computeWriteToRead(commandBuffer);

    uint32_t gx = objects.gridSize/workGroupSize + glm::sign(static_cast<float>(objects.gridSize % workGroupSize));
    globals.cpu->numCellIndices = 0;
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    sets[2] = stagingDescriptorSet;

    vkCmdFillBuffer(commandBuffer, objects.cellIndexArray, 0, VK_WHOLE_SIZE, 0);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.compactCellIndexArray.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.compactCellIndexArray.pipeline.handle);
    vkCmdDispatch(commandBuffer, gx, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);

}

void Collision3D::resolveCollision(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    sets[2] = stagingDescriptorSet;

    for(uint32_t pass = 0; pass < 8; ++pass) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.collisionTest.layout.handle,0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.collisionTest.pipeline.handle);
        vkCmdPushConstants(commandBuffer, compute.collisionTest.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &pass);
        vkCmdDispatchIndirect(commandBuffer, objects.dispatchBuffer, Dispatch::CellArrayIndexCmd);
        Barrier::computeWriteToRead(commandBuffer);
    }
}

void Collision3D::renderGrid(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;


    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.grid.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.grid.layout.handle,0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);

    if(debug.outline) {
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &cell.outline.vertices.buffer, &offset);
    }else{
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &cell.solid.vertices.buffer, &offset);
        vkCmdBindIndexBuffer(commandBuffer, cell.solid.indexes, 0, VK_INDEX_TYPE_UINT32);
    }

    auto size = glm::ivec3((domain.upper - domain.lower)/globals.cpu->spacing);

    for(auto z = 0; z < size.z; ++z) {
        for(auto y = 0; y < size.y; ++y) {
            for(auto x = 0; x < size.x; ++x) {
                auto c = domain.lower + glm::vec3(x, y, z) * globals.cpu->spacing;
                auto p = glm::vec3{c} + 0.5f;
                glm::mat4 transform = glm::translate(glm::mat4{1}, p);
                transform = glm::scale(transform, glm::vec3(globals.cpu->spacing));


                camera->push(commandBuffer, render.grid.layout, transform);
                vkCmdPushConstants(commandBuffer, render.grid.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(Camera), sizeof(glm::ivec3), &c);

                if(debug.outline){
                    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
                    vkCmdDraw(commandBuffer, cell.outline.vertices.sizeAs<Vertex>(), 1, 0, 0);
                }else {
                    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
                    vkCmdDrawIndexed(commandBuffer, cell.solid.indexes.sizeAs<uint32_t>(), 1, 0, 0, 0);
                }
            }
        }
    }

    static glm::vec3 bmin{};
}

void Collision3D::createGizmo() {
//    std::vector<Vertex> vertices {
//        { .position = {-0.5, 0, 0, 1}, .color = {0, 0, 1, 1} },
//        { .position = {0.5, 0, 0, 1}, .color = {0, 0, 1, 1} },
//        { .position = {0, -0.5, 0, 1}, .color = {0, 1, 0, 1} },
//        { .position = {0, 0.5, 0, 1}, .color = {0, 1, 0, 1} },
//        { .position = {0, 0, -0.5, 1}, .color = {1, 0, 0, 1} },
//        { .position = {0, 0, 0.5, 1}, .color = {1, 0, 0, 1} },
//    };
    std::vector<Vertex> vertices {
        { .position = {0, 0, 0, 1}, .color = {0, 0, 1, 1} },
        { .position = {1, 0, 0, 1}, .color = {0, 0, 1, 1} },
        { .position = {0, 0, 0, 1}, .color = {0, 1, 0, 1} },
        { .position = {0, 1, 0, 1}, .color = {0, 1, 0, 1} },
        { .position = {0, 0, 0, 1}, .color = {1, 0, 0, 1} },
        { .position = {0, 0, 1, 1}, .color = {1, 0, 0, 1} },
    };
    
    gizmo.vertices = device.createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void Collision3D::renderGizmo(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.flat.pipeline.handle);
    camera->push(commandBuffer, render.flat.layout, gizmo.transform);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &gizmo.vertices.buffer, &offset);
    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    vkCmdSetDepthCompareOp(commandBuffer, VK_COMPARE_OP_ALWAYS);
    vkCmdDraw(commandBuffer, gizmo.vertices.sizeAs<Vertex>(), 1, 0, 0);
}

void Collision3D::endFrame() {
    static bool once = true;

    if(debug.enabled) {
        device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
            processCollisions(commandBuffer);
        });
    }
    if ((debug.enabled && once) || displayStatus) {

        auto d = expand(globals.cpu->domain, globals.cpu->spacing);
        glm::uvec3 dim{((d.upper - d.lower)/globals.cpu->spacing) };

        const auto numObjects = globals.cpu->numObjects;
        auto cellIds = prevCellIds.span<uint32_t>(numObjects * 8);
        auto attributes = prevAttributes.span<Attribute>(numObjects * 8);
        spdlog::info("num cells: {}", globals.cpu->numCells);

        const auto N = globals.cpu->numObjects;
        for (int i = 0; i < N; i++) {
            spdlog::info("i: {}, id: {}, cells: [{}, {}, {}, {}, {}, {}, {}, {}], controlBits : {:011b} => {}", i,
                         attributes[i].objectID,
                         cellIds[i], cellIds[i + N], cellIds[i + N * 2], cellIds[i + N * 3], cellIds[i + N * 4],
                         cellIds[i + N * 5], cellIds[i + N * 6], cellIds[i + N * 7],
                         attributes[i].controlBits, attributes[i].controlBits);

//                for (auto j = 0; j < 8; ++j) {
//                    auto &d = debug.info[i];
//                    spdlog::info("c: {}, b: [{}, {}], overlap: {}", d.center[j], d.min[j], d.max[j], d.overlap[j]);
//                }
        }

        spdlog::info("");
        spdlog::info("Initial Cell ID Array");
        for(int i = 0; i < 8; ++i){
            std::string row{};
            for(int j = 0; j < globals.cpu->numObjects; ++j){
                int idx = j + globals.cpu->numObjects * i;
                if(cellIds[idx] == 0xFFFFFFFF){
                    row += fmt::format("[     ] ");
                }else {
                    auto cell = cellIds[idx];
                    uint32_t cellType = CELL_TYPE_INDEX(cell % dim.x, (cell/dim.x) % dim.y, cell / (dim.x * dim.y));
                    uint32_t homeCellType = attributes[j].controlBits & HOME_CELL_MASK;
                    if(cellType == homeCellType){
                        auto label = fmt::format(bg(fmt::color::yellow), "[{:02}|{:02}]", cellIds[idx], attributes[j].objectID);
                        label = fmt::format(fg(fmt::color::black), "{}", label);
                        row += fmt::format("{} ", label);
                    }else {
                        auto label = fmt::format(bg(fmt::color::green), "[{:02}|{:02}]", cellIds[idx], attributes[j].objectID);
                        label = fmt::format(fg(fmt::color::black), "{}", label);
                        row += fmt::format("{} ", label);
                    }
                }
            }
            spdlog::info("{}", row);
        }

        std::string row{};
        std::span<CellInfo> cellInfos = { reinterpret_cast<CellInfo*>(objects.cellIndexArray.map()), static_cast<size_t>(globals.cpu->numCellIndices) };

        cellIds = objects.cellIds.span<uint32_t>(numObjects * 8);
        attributes = objects.attributes.span<Attribute>(numObjects * 8);
        spdlog::info("");
        spdlog::info("Sorted Cell ID Array");
        for(int i = 0; i < 8; ++i){
            std::string row{};
            for(int j = 0; j < globals.cpu->numObjects; ++j){
                int idx = j * 8 + i;
                if(cellIds[idx] == 0xFFFFFFFF){
                    row += fmt::format("[     ] ");
                }else {
                    auto cell = cellIds[idx];
                    uint32_t cellType = CELL_TYPE_INDEX(cell % dim.x, (cell/dim.x) % dim.y, cell / (dim.x * dim.y));
                    uint32_t homeCellType = attributes[j].controlBits & HOME_CELL_MASK;
                    if(cellType == homeCellType){
                        auto label = fmt::format(bg(fmt::color::yellow), "[{:02}|{:02}]", cellIds[idx], attributes[idx].objectID);
                        label = fmt::format(fg(fmt::color::black), "{}", label);
                        row += fmt::format("{} ", label);
                    }else {
                        auto label = fmt::format(bg(fmt::color::green), "[{:02}|{:02}]", cellIds[idx], attributes[idx].objectID);
                        label = fmt::format(fg(fmt::color::black), "{}", label);
                        row += fmt::format("{} ", label);
                    }
                }
            }
            spdlog::info("{}", row);
        }

        row = "";
        spdlog::info("");
        spdlog::info("Cell Index Array");
        for(auto cell : cellInfos) {
            std::string label = fmt::format(bg(fmt::color::cyan), "[{:02}|{}+{}]", cell.index, cell.numHomeCells, cell.numPhantomCells);
            label = fmt::format(fg(fmt::color::black), "{}", label);
            row += fmt::format("{} ", label);
        }
        spdlog::info(row);
//            objects.cellIndexArray.unmap();
//            auto cellInfoBC = objects.cellIndexStaging.span<CellInfo>();
//            auto bitSet = objects.bitSet.span<uint32_t>();
//            auto compactIndices = objects.compactIndices.span<uint32_t>();
//            auto dispatch = objects.dispatchBuffer.span<glm::uvec4>();
//            auto offsets = objects.counts.span<uint32_t>();
//            auto updates = updatesBuffer.span<UpdateInfo>(globals.cpu->numUpdates);
//            auto positions = objects.position.span<glm::vec2>();
//
//            spdlog::info("");
//            for(int i = 0; i < globals.cpu->numUpdates; i += 2){
//                auto a = updates[i];
//                for(auto j = 0; j < globals.cpu->numUpdates; j++){
//                    if(i == j) continue;
//                    auto b = updates[j];
//
//                    if(a.tid != b.tid && a.objectId == b.objectId && a.pass == b.pass) {
//                        b = updates[j % 2 == 0 ? j + 1 : j - 1];
//                        glm::uvec2 gpa = glm::uvec2(glm::floor(positions[a.objectId]/globals.cpu->spacing));
//                        glm::uvec2 gpb = glm::uvec2(glm::floor(positions[b.objectId]/globals.cpu->spacing));
//                        spdlog::info("[{}, {}]: [[{}, {}] | {}] <=> [{}, {}] | {}  updated by threads {} & {} in pass {}, cellInfo: [{}, {}]"
//                                ,a.objectId, b.objectId,gpa.x, gpa.y,  positions[a.objectId], gpb.x, gpb.y,  positions[b.objectId], a.tid, b.tid, a.pass, a.cellID, b.cellID);
//                    }
//                }
//            }
        spdlog::info("");
        once = false;
        displayStatus = false;
    }

    if(pauseRequested) {
        pauseSim = !pauseSim;
        pauseRequested = false;
    }
}

uint32_t Collision3D::gridSize() {
    auto d = expand(globals.cpu->domain, globals.cpu->spacing);
    glm::uvec3 dim{((d.upper - d.lower)/globals.cpu->spacing) };
    return objects.gridSize = dim.x * dim.y * dim.z;
}

int main(){
    try{
        fs::current_path("../../../../examples/");
        spdlog::info("current working directory: {}", fs::current_path().string());
        Settings settings;
        settings.width = 1440;
        settings.height = 1280;
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
        auto app = Collision3D{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}