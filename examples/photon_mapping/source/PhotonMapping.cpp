#include "PhotonMapping.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"

PhotonMapping::PhotonMapping(const Settings& settings) : VulkanRayTraceBaseApp("Photon mapping", settings) {
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../data/ltc");
    fileManager.addSearchPathFront("../../data/models");
    fileManager.addSearchPathFront("../../examples/photon_mapping");
    fileManager.addSearchPathFront("../../examples/photon_mapping/data");
    fileManager.addSearchPathFront("../../examples/photon_mapping/spv");
    fileManager.addSearchPathFront("../../examples/photon_mapping/models");
    fileManager.addSearchPathFront("../../examples/photon_mapping/textures");
}

void PhotonMapping::initApp() {
    createDescriptorPool();
    loadLTC();
    loadModel();
    initLight();
    initCamera();
    initCanvas();
    initRayTraceInfo();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    updateRtDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
    createRayTracingPipeline();
}

void PhotonMapping::loadModel() {
    auto cornellBox = primitives::cornellBox();

    for(auto& mesh : cornellBox) {
        for(auto& vertex : mesh.vertices) {
            vertex.position  = glm::vec4(vertex.position.xyz() * 0.1f, 1);
        }
    }


    std::vector<mesh::Mesh> meshes(10);
    meshes[0].name = "Light";
    meshes[0].vertices = cornellBox[0].vertices;
    meshes[0].indices = cornellBox[0].indices;
    meshes[0].material.name = "Light";
    meshes[0].material.ambient = glm::vec3(0);
    meshes[0].material.diffuse = glm::vec3(0);
    meshes[0].material.specular = glm::vec3(0);
    meshes[0].material.emission = radiance;
    meshes[0].material.shininess = 1;


    auto center = mesh::center(meshes[0]);
    glm::vec3 min, max;
    mesh::bounds({ meshes[0]}, min, max);
    auto radius = 5.0f;
    glm::mat4 xform = glm::translate(glm::mat4{1}, {0, radius * 5.f, 0});
    xform = glm::translate(xform, center);
    auto sphere = primitives::sphere(1000, 1000, radius, xform, color::white, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
//    meshes[0].vertices = sphere.vertices;
//    meshes[0].indices = sphere.indices;


    meshes[1].name = "Floor";
    meshes[1].vertices = cornellBox[3].vertices;
    meshes[1].indices = cornellBox[3].indices;
    meshes[1].material.name = "Floor";
    meshes[1].material.diffuse = cornellBox[3].vertices[0].color;
    meshes[1].material.specular = glm::vec3(0);
    meshes[1].material.shininess = 1;

    meshes[2].name = "Celling";
    meshes[2].vertices = cornellBox[1].vertices;
    meshes[2].indices = cornellBox[1].indices;
    meshes[2].material.name = "Celling";
    meshes[2].material.diffuse = cornellBox[1].vertices[0].color;
    meshes[2].material.specular = glm::vec3(0);
    meshes[2].material.shininess = 1;

    meshes[3].name = "BackWall";
    meshes[3].vertices = cornellBox[7].vertices;
    meshes[3].indices = cornellBox[7].indices;
    meshes[3].material.name = "BackWall";
    meshes[3].material.diffuse = cornellBox[7].vertices[0].color;
    meshes[3].material.specular = glm::vec3(0);
    meshes[3].material.shininess = 1;

    meshes[4].name = "rightWall";
    meshes[4].vertices = cornellBox[2].vertices;
    meshes[4].indices = cornellBox[2].indices;
    meshes[4].material.name = "rightWall";
    meshes[4].material.diffuse = cornellBox[2].vertices[0].color;
    meshes[4].material.specular = glm::vec3(0);
    meshes[4].material.shininess = 1;

    meshes[5].name = "LeftWall";
    meshes[5].vertices = cornellBox[4].vertices;
    meshes[5].indices = cornellBox[4].indices;
    meshes[5].material.name = "LeftWall";
    meshes[5].material.diffuse = cornellBox[4].vertices[0].color;
    meshes[5].material.specular = glm::vec3(0);
    meshes[5].material.shininess = 1;



    meshes[6].name = "ShortBox";
    meshes[6].vertices = cornellBox[6].vertices;
    meshes[6].indices = cornellBox[6].indices;
    meshes[6].material.name = "ShortBox";
    meshes[6].material.diffuse = cornellBox[6].vertices[0].color;
    meshes[6].material.specular = glm::vec3(0);
    meshes[6].material.shininess = 1;
    meshes[6].material.opacity = 10;



    meshes[7].name = "TallBox";
    meshes[7].vertices = cornellBox[5].vertices;
    meshes[7].indices = cornellBox[5].indices;
    meshes[7].material.name = "TallBox";
    meshes[7].material.diffuse = cornellBox[5].vertices[0].color;
    meshes[7].material.specular = glm::vec3(0);
    meshes[7].material.shininess = 1;
    meshes[7].material.opacity = 10;


    mesh::bounds({ meshes[7]}, min, max);
    mesh::bounds({ meshes[7]}, min, max);
    center = (min + max) * 0.5f;
    xform = glm::translate(glm::mat4{1}, center);
    sphere = primitives::sphere(1000, 1000, radius, xform, color::white, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
//    meshes[7].vertices = sphere.vertices;
//    meshes[7].indices = sphere.indices;


    mesh::bounds({ meshes[6]}, min, max);
    auto boxPos = (min + max) *.5f;
    mesh::bounds({ meshes[1] }, min, max);
    auto floorPos = min;
    radius = 1;
    center = glm::vec3(boxPos.x, floorPos.y + radius, boxPos.z);
    xform = glm::translate(glm::mat4{1}, center);
    sphere = primitives::sphere(1000, 1000, radius, xform, color::white, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    meshes[8].name = "FrontSphere";
    meshes[8].vertices = sphere.vertices;
    meshes[8].indices = sphere.indices;
    meshes[8].material.name = "FrontSphere";
    meshes[8].material.diffuse = cornellBox[6].vertices[0].color;
    meshes[8].material.specular = glm::vec3(1);
    meshes[8].material.shininess = 100;
    meshes[8].material.opacity = 1;

    mesh::bounds({ meshes[7]}, min, max);
    boxPos = (min + max) *.5f;

    center = glm::vec3(boxPos.x, center.y, boxPos.z);
    xform = glm::translate(glm::mat4{1}, center);
    sphere = primitives::sphere(1000, 1000, radius, xform, color::white, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    meshes[9].name = "BackSphere";
    meshes[9].vertices = sphere.vertices;
    meshes[9].indices = sphere.indices;
    meshes[9].material.name = "FrontSphere";
    meshes[9].material.diffuse = cornellBox[6].vertices[0].color;
    meshes[9].material.specular = glm::vec3(1);
    meshes[9].material.shininess = 100;
    meshes[9].material.opacity = 1;



//    mesh::normalize(meshes);

    phong::VulkanDrawableInfo info{};
    info.vertexUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.indexUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.materialUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.materialIdUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.generateMaterialId = true;
    info.vertexUsage += VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.indexUsage += VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    info.materialBufferMemoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    phong::load(device, descriptorPool, model, meshes, info);

    rt::MeshObjectInstance instance{};
    instance.object = rt::TriangleMesh{ &model };
    std::for_each(instance.object.metaData.begin(), instance.object.metaData.end(), [](auto& md){ md.mask = ObjectMask::Primitives; });
    instance.object.metaData[0].mask = ObjectMask::Lights;

    instance.object.metaData[6].mask = ObjectMask::Box;
    instance.object.metaData[6].hitGroupId = HitGroups::Glass;

    instance.object.metaData[7].mask = ObjectMask::Box;
    instance.object.metaData[7].hitGroupId = HitGroups::Mirror;

    instance.object.metaData[8].mask = ObjectMask::Sphere;
    instance.object.metaData[8].hitGroupId = HitGroups::Glass;

    instance.object.metaData[9].mask = ObjectMask::Sphere;
    instance.object.metaData[9].hitGroupId = HitGroups::Mirror;
    instances.push_back(instance);
    createAccelerationStructure(instances);
}

void PhotonMapping::initLight() {
    glm::vec4 lowerCorner{MAX_FLOAT};
    glm::vec4 upperCorner{MIN_FLOAT};
    auto mesh = primitives::cornellBox().front();

    for(auto corner : mesh.vertices) {
        lowerCorner = glm::min(lowerCorner, corner.position * 0.1f);
        upperCorner = glm::max(upperCorner, corner.position * 0.1f);
    }

    glm::vec3 intensity = radiance;
    LightData ld{};
    ld.position = (lowerCorner + upperCorner) * 0.5f;
    ld.normal = glm::vec4(mesh.vertices.front().normal, 0);
    ld.lowerCorner = lowerCorner;
    ld.upperCorner = upperCorner;
    ld.intensity = glm::vec4(intensity, 0);

    light.gpu = device.createCpuVisibleBuffer(&ld, sizeof(LightData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    light.cpu = reinterpret_cast<LightData*>(light.gpu.map());
}

void PhotonMapping::loadLTC() {
    auto buffer = loadFile(resource("ltc_mat.dat"));
    textures::create(device, ltc.mat, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, buffer.data(), {64, 64, 1});

    buffer = loadFile(resource("ltc_amp.dat"));
    textures::create(device, ltc.amp, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SFLOAT, buffer.data(),  {64, 64, 1});

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    ltc.sampler = device.createSampler(samplerInfo);
}

void PhotonMapping::initCamera() {
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


void PhotonMapping::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 13> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
                    { VK_DESCRIPTOR_TYPE_SAMPLER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 100 * maxSets }
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void PhotonMapping::createDescriptorSetLayouts() {
    raytrace.descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("ray_trace")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_RAYGEN_BIT_KHR)
        .createLayout();
    
    const auto numInstances = instances.size();

    raytrace.instanceDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("materials_descriptor_set_layout")
            .binding(0) // materials
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(numInstances)
                .shaderStages(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
            .binding(1) // materialId
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(numInstances)
                .shaderStages(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
            .binding(2) // scene objects
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
        .createLayout();

    raytrace.vertexDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("raytrace_vertex")
            .binding(0) // vertex buffer binding
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(numInstances)
                .shaderStages(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
            .binding(1)     // index buffer bindings
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(numInstances)
                .shaderStages(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
            .binding(2)     // vertex offset buffer
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(numInstances)
                .shaderStages(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
            .createLayout();


    lightDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("photon_mapping_globals")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .createLayout();
}

void PhotonMapping::updateDescriptorSets(){
    lightDescriptorSet = descriptorPool.allocate( { lightDescriptorSetLayout }).front();

    auto writes = initializers::writeDescriptorSets<3>();


    writes[0].dstSet = lightDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo lightInfo{light.gpu, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &lightInfo;

    writes[1].dstSet = lightDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo ltcMagInfo{ltc.mat.sampler.handle, ltc.mat.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[1].pImageInfo = &ltcMagInfo;

    writes[2].dstSet = lightDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo ltcAmpInfo{ltc.amp.sampler.handle, ltc.amp.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[2].pImageInfo = &ltcAmpInfo;

    device.updateDescriptorSets(writes);
}

void PhotonMapping::updateRtDescriptorSets() {
    auto sets = descriptorPool.allocate({ 
        raytrace.descriptorSetLayout, 
        raytrace.instanceDescriptorSetLayout, 
        raytrace.vertexDescriptorSetLayout});
    
    raytrace.descriptorSet = sets[0];
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("raytrace_main_set", raytrace.descriptorSet);

    raytrace.instanceDescriptorSet = sets[1];
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("raytrace_instance_set", raytrace.instanceDescriptorSet);

    raytrace.vertexDescriptorSet = sets[2];
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("raytrace_vertex_set", raytrace.vertexDescriptorSet);

    auto writes = initializers::writeDescriptorSets<3>();
    VkWriteDescriptorSetAccelerationStructureKHR asWrites{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    asWrites.accelerationStructureCount = 1;
    asWrites.pAccelerationStructures =  rtBuilder.accelerationStructure();
    writes[0].pNext = &asWrites;
    writes[0].dstSet = raytrace.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writes[0].descriptorCount = 1;

    writes[1].dstSet = raytrace.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo camInfo{ rayTraceInfo.gpu, 0, VK_WHOLE_SIZE};
    writes[1].pBufferInfo = &camInfo;

    writes[2].dstSet = raytrace.descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo imageInfo{ VK_NULL_HANDLE, canvas.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &imageInfo;

    device.updateDescriptorSets(writes);

    // raytrace instance data
    writes = initializers::writeDescriptorSets<3>();

    writes[0].dstSet = raytrace.instanceDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo materialBufferInfo{model.materialBuffer, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &materialBufferInfo;
    

    writes[1].dstSet = raytrace.instanceDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo matIdBufferInfo{ model.materialIdBuffer, 0, VK_WHOLE_SIZE};
    writes[1].pBufferInfo = &matIdBufferInfo;

    VkDescriptorBufferInfo sceneBufferInfo{};
    sceneBufferInfo.buffer = sceneObjectBuffer;
    sceneBufferInfo.offset = 0;
    sceneBufferInfo.range = VK_WHOLE_SIZE;

    writes[2].dstSet= raytrace.instanceDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    writes[2].pBufferInfo = &sceneBufferInfo;

    device.updateDescriptorSets(writes);

    // raytrace vertex data
    writes = initializers::writeDescriptorSets<3>();

    writes[0].dstSet = raytrace.vertexDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo vertexBufferInfo{model.vertexBuffer, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &vertexBufferInfo;

    writes[1].dstSet = raytrace.vertexDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo indexBufferInfo{ model.indexBuffer, 0, VK_WHOLE_SIZE};
    writes[1].pBufferInfo = &indexBufferInfo;


    writes[2].dstSet = raytrace.vertexDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo vertexOffsetInfo{ model.offsetBuffer, 0, VK_WHOLE_SIZE};
    writes[2].pBufferInfo = &vertexOffsetInfo;


    device.updateDescriptorSets(writes);

}

void PhotonMapping::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void PhotonMapping::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}

void PhotonMapping::initCanvas() {
    canvas = Canvas{ this, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_FORMAT_R8G8B8A8_UNORM};
    canvas.init();
    std::vector<unsigned char> checkerboard(width * height * 4);
    textures::checkerboard1(checkerboard.data(), {width, height});
    const auto stagingBuffer = device.createCpuVisibleBuffer(checkerboard.data(), BYTE_SIZE(checkerboard), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        textures::transfer(commandBuffer, stagingBuffer, canvas.image, {width, height}, VK_IMAGE_LAYOUT_GENERAL);
    });
}

void PhotonMapping::initRayTraceInfo() {
    rayTraceInfo.gpu = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(RayTraceData));
    rayTraceInfo.cpu = reinterpret_cast<RayTraceData*>(rayTraceInfo.gpu.map());
    rayTraceInfo.cpu->mask = ObjectMask::All & ~ObjectMask::Box;
}

void PhotonMapping::createRayTracingPipeline() {
    auto rayGenShaderModule = device.createShaderModule( resource("raygen.rgen.spv"));
    auto mainClosestHitShaderModule = device.createShaderModule(resource("main.rchit.spv"));
    auto mirrorClosestHitShaderModule = device.createShaderModule(resource("mirror.rchit.spv"));
    auto glassClosestHitShaderModule = device.createShaderModule(resource("glass.rchit.spv"));
    auto mainMissShaderModule = device.createShaderModule(resource("main.rmiss.spv"));
    auto shadowMissShaderModule = device.createShaderModule(resource("shadow.rmiss.spv"));

    std::vector<ShaderInfo> shaders(ShaderCount);

    shaders[RayGen] = { rayGenShaderModule, VK_SHADER_STAGE_RAYGEN_BIT_KHR};
    shaders[MainMiss] = { mainMissShaderModule, VK_SHADER_STAGE_MISS_BIT_KHR};
    shaders[LightMiss] = { shadowMissShaderModule, VK_SHADER_STAGE_MISS_BIT_KHR};
    shaders[MainClosestHit] = { mainClosestHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    shaders[MirrorClosestHit] = { mirrorClosestHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    shaders[GlassClosestHit] = { glassClosestHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
    shaderGroups.push_back(shaderTablesDesc.rayGenGroup(RayGen));
    shaderGroups.push_back(shaderTablesDesc.addMissGroup(MainMiss));
    shaderGroups.push_back(shaderTablesDesc.addMissGroup(LightMiss));

    shaderGroups.push_back(shaderTablesDesc.addHitGroup(MainClosestHit));
    shaderGroups.push_back(shaderTablesDesc.addHitGroup(MirrorClosestHit));
    shaderGroups.push_back(shaderTablesDesc.addHitGroup(GlassClosestHit));

    dispose(raytrace.layout);

    raytrace.layout = device.createPipelineLayout({
        raytrace.descriptorSetLayout,
        raytrace.instanceDescriptorSetLayout,
        raytrace.vertexDescriptorSetLayout,
        lightDescriptorSetLayout
    });

    auto stages = initializers::rayTraceShaderStages(shaders);
    VkRayTracingPipelineCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
    createInfo.stageCount = COUNT(stages);
    createInfo.pStages = stages.data();
    createInfo.groupCount = COUNT(shaderGroups);
    createInfo.pGroups = shaderGroups.data();
    createInfo.maxPipelineRayRecursionDepth = 1;
    createInfo.layout = raytrace.layout.handle;

    raytrace.pipeline = device.createRayTracingPipeline(createInfo);
    bindingTables = shaderTablesDesc.compile(device, raytrace.pipeline);
}

void PhotonMapping::rayTrace(VkCommandBuffer commandBuffer) {
    CanvasToRayTraceBarrier(commandBuffer);

    std::vector<VkDescriptorSet> sets{
        raytrace.descriptorSet,
        raytrace.instanceDescriptorSet,
        raytrace.vertexDescriptorSet,
        lightDescriptorSet
    };
    assert(raytrace.pipeline);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytrace.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytrace.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);

    vkCmdTraceRaysKHR(commandBuffer, bindingTables.rayGen, bindingTables.miss, bindingTables.closestHit,
                      bindingTables.callable, swapChain.extent.width, swapChain.extent.height, 1);

    rayTraceToCanvasBarrier(commandBuffer);
}

void PhotonMapping::rasterize(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    camera->push(commandBuffer, render.layout);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 1, 1, &lightDescriptorSet, 0, nullptr);
    model.draw(commandBuffer, render.layout);
}

void PhotonMapping::rayTraceToCanvasBarrier(VkCommandBuffer commandBuffer) const {
    VkImageMemoryBarrier barrier = initializers::ImageMemoryBarrier();
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = canvas.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;


    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,

                         0,
                         VK_NULL_HANDLE,
                         0,
                         VK_NULL_HANDLE,
                         1,
                         &barrier);
}

void PhotonMapping::CanvasToRayTraceBarrier(VkCommandBuffer commandBuffer) const {
    VkImageMemoryBarrier barrier = initializers::ImageMemoryBarrier();
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = canvas.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    VkMemoryBarrier mBarrier{};
    mBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    mBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         0,
                         0,
                         VK_NULL_HANDLE,
                         0,
                         VK_NULL_HANDLE,
                         1,
                         &barrier);
}


void PhotonMapping::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("render.vert.spv"))
                    .fragmentShader(resource("render.frag.spv"))
                .rasterizationState()
                    .cullNone()
                .layout().clear()
                    .addPushConstantRange(Camera::pushConstant())
                    .addDescriptorSetLayout(model.descriptorSetLayout)
                    .addDescriptorSetLayout(lightDescriptorSetLayout)
                .name("render")
                .build(render.layout);
    //    @formatter:on
}

void PhotonMapping::createComputePipeline() {
    auto module = device.createShaderModule( "../../data/shaders/pass_through.comp.spv");
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute.layout = device.createPipelineLayout();

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.layout.handle;

    compute.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
}


void PhotonMapping::onSwapChainDispose() {
    dispose(render.pipeline);
    dispose(compute.pipeline);
    dispose(raytrace.pipeline);
}

void PhotonMapping::onSwapChainRecreation() {
    initCanvas();
    createRayTracingPipeline();
    updateDescriptorSets();
    createRenderPipeline();
    createComputePipeline();
}

VkCommandBuffer *PhotonMapping::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    static std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = {0, 0, 0, 1};
    clearValues[1].depthStencil = {1.0, 0u};

    VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
    rPassInfo.clearValueCount = COUNT(clearValues);
    rPassInfo.pClearValues = clearValues.data();
    rPassInfo.framebuffer = framebuffers[imageIndex];
    rPassInfo.renderArea.offset = {0u, 0u};
    rPassInfo.renderArea.extent = swapChain.extent;
    rPassInfo.renderPass = renderPass;

    vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);
//    rasterize(commandBuffer);
    canvas.draw(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    rayTrace(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void PhotonMapping::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
    rayTraceInfo.cpu->viewInverse = glm::inverse(cam.view);
    rayTraceInfo.cpu->projectionInverse = glm::inverse(cam.proj);

}

void PhotonMapping::checkAppInputs() {
    camera->processInput();
}

void PhotonMapping::cleanup() {
    VulkanBaseApp::cleanup();
}

void PhotonMapping::onPause() {
    VulkanBaseApp::onPause();
}


int main(){
    try{

        Settings settings;
        settings.depthTest = true;
        settings.enableBindlessDescriptors = false;
        settings.deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = PhotonMapping{ settings };
        app.addPlugin(imGui);

        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}