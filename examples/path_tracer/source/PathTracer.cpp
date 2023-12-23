#include "PathTracer.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include <assimp/Exporter.hpp>
#include "ImGuiPlugin.hpp"
#include "spectrum/spectrum.hpp"
#include <stb_image_write.h>
#include "denoiser.hpp"

PathTracer::PathTracer(const Settings& settings) : VulkanRayTraceBaseApp("reference path tracer", settings) {
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/path_tracer");
    fileManager.addSearchPathFront("../../examples/path_tracer/spv");
    fileManager.addSearchPathFront("../../examples/path_tracer/models");
    fileManager.addSearchPathFront("../../examples/path_tracer/textures");
    fileManager.addSearchPathFront("../../examples/path_tracer/environment");
    fileManager.addSearchPathFront("../../data/shaders");
    fileManager.addSearchPathFront("../../data/models");
    fileManager.addSearchPathFront("../../data/textures");
    fileManager.addSearchPathFront("../../data");
    fileManager.addSearchPathFront(R"(C:\Users\Josiah Ebhomenye\OneDrive\media\models)");


    timelineFeatures = VkPhysicalDeviceTimelineSemaphoreFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
        &enabledDescriptorIndexingFeatures,
        VK_TRUE
    };

    rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    rayQueryFeatures.rayQuery = VK_TRUE;
    rayQueryFeatures.pNext = &timelineFeatures;

    syncFeatures = VkPhysicalDeviceSynchronization2Features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
        &rayQueryFeatures,
        VK_TRUE
    };
    deviceCreateNextChain = &syncFeatures;
}

void PathTracer::initApp() {
    optix = std::make_shared<OptixContext>();
    initDenoiser();
    initShapes();
    loadEnvironmentMap();
    initCamera();
    initCanvas();
    createInverseCam();
    createDescriptorPool();
    loadMediums();
    loadModel();
    initLights();
    loadDragon();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRayTracingPipeline();
    createPostProcessPipeline();
    m.invalidateSwapChain = &swapChainInvalidated;
    m.instances = &instances;
    gui = Gui{&plugin<ImGuiPlugin>(IM_GUI_PLUGIN), &m};

    addKeyReleaseListener([&](const KeyEvent&){
        const int F11 = 300;
        const int F12 = 301;
        if(keyEvent.getCode() == F12){
            gui.hide  = !gui.hide;
        }
        if(keyEvent.getCode() == F11){
            gui.takeScreenShot = true;

        }
    });
}

void PathTracer::initShapes() {
    int count = 10;
    shapes.rectangles = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                            VMA_MEMORY_USAGE_CPU_TO_GPU,
                                            count * sizeof(Rectangle_));
    shapes.spheres = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                         VMA_MEMORY_USAGE_CPU_TO_GPU,
                                         count * sizeof(Sphere));

    shapes.disks = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                         VMA_MEMORY_USAGE_CPU_TO_GPU,
                                         count * sizeof(Disk));
    lightShapeRef = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                         VMA_MEMORY_USAGE_CPU_TO_GPU,
                                         100 * sizeof(ShapeRef));
}

void PathTracer::initDenoiser() {
    textures::create(device, rayTracedTexture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT,
                     {swapChain.width(), swapChain.height(), 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT, sizeof(float));
    device.graphicsCommandPool().oneTimeCommand([&](auto cb){
        rayTracedTexture.image.transitionLayout(cb, VK_IMAGE_LAYOUT_GENERAL, DEFAULT_SUB_RANGE, VK_ACCESS_NONE, VK_ACCESS_NONE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_NONE);
    });
    textures::create(device, denoiserGuide.albedo, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {swapChain.width(), swapChain.height(), 1},
                     VK_SAMPLER_ADDRESS_MODE_REPEAT, sizeof(float));
    textures::create(device, denoiserGuide.normal, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {swapChain.width(), swapChain.height(), 1},
                     VK_SAMPLER_ADDRESS_MODE_REPEAT, sizeof(float));
    textures::create(device, denoiserGuide.flow, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {swapChain.width(), swapChain.height(), 1},
                     VK_SAMPLER_ADDRESS_MODE_REPEAT, sizeof(float));
    
    denoiserGuide.albedo.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    denoiserGuide.normal.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    denoiserGuide.flow.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    
    device.graphicsCommandPool().oneTimeCommand([&](auto cb){
        VkClearColorValue clearValue{0.f, 0.f, 0.f, 0.f};
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cb, denoiserGuide.albedo.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &range);
        vkCmdClearColorImage(cb, denoiserGuide.normal.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &range);
        vkCmdClearColorImage(cb, denoiserGuide.flow.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &range);
    });

    VkDeviceSize size = rayTracedTexture.image.size;
    auto bufferUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    cuda::Buffer colorBuffer{ device, device.createExportableBuffer(bufferUsage, VMA_MEMORY_USAGE_CPU_TO_GPU, size)};
    cuda::Buffer normalBuffer{ device, device.createExportableBuffer(bufferUsage, VMA_MEMORY_USAGE_CPU_TO_GPU, size)};
    cuda::Buffer albedoBuffer{ device, device.createExportableBuffer(bufferUsage, VMA_MEMORY_USAGE_CPU_TO_GPU, size)};

    std::vector<cuda::Buffer> outputs;
    outputs.emplace_back(device, device.createExportableBuffer(bufferUsage, VMA_MEMORY_USAGE_CPU_TO_GPU, size));

     VulkanDenoiser::Data data = VulkanDenoiser::Data{
            static_cast<uint32_t>(swapChain.width()),
            static_cast<uint32_t>(swapChain.height()),
            colorBuffer,
            albedoBuffer,
            normalBuffer,
            outputs,
    };
    VulkanDenoiser::Settings settings{};
    denoiser = std::make_unique<VulkanDenoiser>( optix, data, settings);
    denoiseSemaphore = cuda::Semaphore{device};
}

void PathTracer::loadEnvironmentMap() {
//    textures::hdr(device, environmentMap, resource("environment/old_hall_4k.hdr"));
    textures::hdr(device, environmentMap, resource("environment/white.png"));
//    textures::exr(device, environmentMap, resource("sky.exr"));
//    textures::hdr(device, environmentMap, resource("environment/HdrOutdoorFieldWinterDayClear002_JPG_4K.jpg"));
    textures::createDistribution(device, environmentMap, envMapDistribution);
    m.sceneConstants.pMargialIntegral = envMapDistribution.pMarginal.funcIntegral;
}

void PathTracer::initLights() {
    std::vector<Light> lights;

    auto ref = reinterpret_cast<ShapeRef*>(lightShapeRef.map());
    ref[0].objectId = static_cast<int>(lights.size());
    ref[0].shapeId = 0;
    ref[0].shape = static_cast<int>(Shape::Rectangle);

    ref[1].objectId = static_cast<int>(lights.size());
    ref[1].shapeId = 0;
    ref[1].shape = static_cast<int>(Shape::Disk);

    ref[2].objectId = static_cast<int>(lights.size());
    ref[2].shapeId = 0;
    ref[2].shape = static_cast<int>(Shape::Sphere);


    lights.push_back(cornellLight);

    Light light;
    light.value = spectrum::blackbodySpectrum({ 6400, 40}).front();
    light.position = camera->position();
    light.normal = camera->viewDir;
    light.flags = DeltaPosition;
    light.cosWidth = glm::cos(glm::radians(20.f));
    light.fallOffStart = glm::cos(glm::radians(10.f));
    lights.push_back(light);


    light = Light{};
    light.position = {1, 0, 0};
    light.normal = {0, 0, -1};
    light.value = glm::vec3(m.sun.intensity);
    light.flags = DeltaDirection;
    lights.push_back(light);

    light = Light{};
    light.flags = Infinite;
    lights.push_back(light);

    lightsBuffer = device.createCpuVisibleBuffer(lights.data(), BYTE_SIZE(lights), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    m.lights = reinterpret_cast<Light*>(lightsBuffer.map());
    m.numLights = lights.size();
    m.sceneConstants.numLights = 0;
}

void PathTracer::loadMediums() {
    std::vector<Medium> mediums;

    Medium medium{glm::vec3(10), glm::vec3(90), 0.f};
    mediums.push_back(medium);


    mediumBuffer = device.createCpuVisibleBuffer(mediums.data(), BYTE_SIZE(mediums),
                                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    m.mediums = reinterpret_cast<Medium*>(mediumBuffer.map());
}

void PathTracer::loadModel() {
    phong::VulkanDrawableInfo info{};
    info.vertexUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.indexUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.materialUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.materialIdUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.generateMaterialId = true;
    info.vertexUsage += VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.indexUsage += VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    info.materialBufferMemoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    VulkanDrawable drawable;

    createCornellBox(info);
    rt::MeshObjectInstance instance{};
    instance.object = rt::TriangleMesh{ &drawables["cornell"] };
    std::generate(begin(instance.object.metaData), end(instance.object.metaData),
                  []{ return rt::TriangleMesh::MetaData{0, eCornellBox}; });
    instance.object.metaData[0].mask = ObjectTypes::eLights;
    instances.push_back(instance);
    objects.push_back(&drawables["cornell"]);

    glm::mat4 xform = glm::rotate(glm::mat4(1), -glm::half_pi<float>(), {1, 0, 0});
    auto vPlane = primitives::plane(1, 1, 100, 100, xform, {0.8, 0.8, 0.8, 1}, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    mesh::Mesh plane;
    plane.name = "plane";
    plane.vertices = vPlane.vertices;
    plane.indices = vPlane.indices;
    plane.material.name = "plane";
    plane.material.diffuse = glm::vec3(1.0);
    plane.material.ambient = glm::vec3(0);
    plane.material.specular = glm::vec3(0);
    plane.material.emission = glm::vec3(0);
    plane.material.shininess = 1;

    std::vector<mesh::Mesh> meshes{plane};
    phong::load(device, descriptorPool, drawable, meshes, info, false);
    drawables.insert(std::make_pair("plane", std::move(drawable)));

    rt::MeshObjectInstance pInstance{};
    pInstance.object = rt::TriangleMesh{ &drawables["plane"] };
    pInstance.object.metaData[0].hitGroupId = 0;
    pInstance.object.metaData[0].mask = ePlane;
    pInstance.xform = glm::translate(glm::mat4{1}, {0, drawables["cornell"].bounds.min.y, 0});
    m.sceneConstants.planeId = static_cast<int>(instance.object.metaData.size());
    instances.push_back(pInstance);
    objects.push_back(&drawables["plane"]);

    m.cornellMaterials = reinterpret_cast<Material*>(objects[0]->materialBuffer.map());
    m.floorMaterial = reinterpret_cast<Material*>(objects[1]->materialBuffer.map());

    createAccelerationStructure(instances);
}

void PathTracer::loadDragon() {
    dragonLoad = par::ThreadPool::global().async([=]{
        VulkanDrawable drawable;

        phong::VulkanDrawableInfo info{};
        info.vertexUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        info.indexUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        info.materialUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        info.materialIdUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        info.generateMaterialId = true;
        info.vertexUsage += VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        info.indexUsage += VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        info.materialBufferMemoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        phong::load(resource("baby_dragon.obj"), device, descriptorPool, drawable, info, true);
        drawables.insert(std::make_pair("dragon", std::move(drawable)));
    });
}

void PathTracer::createCornellBox(phong::VulkanDrawableInfo info) {

    auto cornellBox = primitives::cornellBox();
//
//    glm::vec3 radiance = color::rgb(252, 234, 182) * 1000.f;
    glm::vec3 radiance = spectrum::blackbodySpectrum({2750, 1000}).front();
    spdlog::info("light: {}", radiance);
////    glm::vec3 radiance = glm::vec3(0);


    std::vector<mesh::Mesh> meshes(8);
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


    mesh::bounds({ meshes[6]}, min, max);
    radius =(max.y - min.y) * 0.5f;
    center = (min + max) * 0.5f;
    xform = glm::translate(glm::mat4{1}, center);
    sphere = primitives::sphere(1000, 1000, radius, xform, color::white, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
//    meshes[6].vertices = sphere.vertices;
//    meshes[6].indices = sphere.indices;


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

    mesh::normalize(meshes);

    auto& rectangle = reinterpret_cast<Rectangle_*>(shapes.rectangles.map())[0];
    rectangle.p0 = meshes[0].vertices[0].position.xyz();
    rectangle.p1 = meshes[0].vertices[1].position.xyz();
    rectangle.p2 = meshes[0].vertices[2].position.xyz();
    rectangle.p3 = meshes[0].vertices[3].position.xyz();

    auto a = rectangle.p1 - rectangle.p0;
    auto b= rectangle.p2 - rectangle.p0;
    auto area0 = glm::length(a) * glm::length(b);
    auto area1 = mesh::surfaceArea(meshes[0]);

    shapes.rectangles.unmap();

    center = mesh::center(meshes[0]);
    mesh::bounds({ meshes[0]}, min, max);
    auto& disk = reinterpret_cast<Disk*>(shapes.disks.map())[0];
    disk.center = center;
    disk.radius = glm::distance(min, max) * 0.5f;
    disk.height = center.y;
    shapes.disks.unmap();

    auto& sphereShape = reinterpret_cast<Sphere*>(shapes.spheres.map())[0];
    sphereShape.center = center;
    sphereShape.radius = glm::distance(min, max) * 0.15f;
    shapes.spheres.unmap();

    cornellLight.value = radiance;
    cornellLight.flags = Area;
    cornellLight.normal = glm::vec3(0, -1, 0);
    cornellLight.shapeType = static_cast<int>(Shape::Rectangle);
    cornellLight.shapeId = 0;


    VulkanDrawable drawable;
    phong::load(device, descriptorPool, drawable, meshes, info);
//    phong::load(resource("cornell_box.obj"), device, descriptorPool, drawable, info, true, 2);

    auto& bounds = drawable.bounds;
    m.sceneConstants.worldRadius = 50;

    auto target = (drawable.bounds.max + drawable.bounds.min) * 0.5f;
    auto dir = glm::normalize(glm::vec3(0, 0, 1) + target);
    auto position = target + dir * 2.f;
    camera->lookAt(position, target, {0, 1, 0});

//    m.cornellMaterials = reinterpret_cast<Material*>(drawable.materialBuffer.map());
    drawables.insert(std::make_pair("cornell", std::move(drawable)));

}

void PathTracer::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.fieldOfView = 60.0f;
//    cameraSettings.horizontalFov = true;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera->lookAt(glm::vec3(-0.194, 0.06, 0.7), glm::vec3(0), {0, 1, 0});
}


void PathTracer::createDescriptorPool() {
    constexpr uint32_t maxSets = 500;
    std::array<VkDescriptorPoolSize, 16> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 500 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 500 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 500 * maxSets},
                    { VK_DESCRIPTOR_TYPE_SAMPLER, 500 * maxSets },
                    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 500 * maxSets },
                    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 500 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 500 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 500 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 500 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 500 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 500 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 500 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 500 * maxSets },
                    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 500 * maxSets },
                    { VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT, 500 * maxSets },
                    { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 500 * maxSets }
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_POOL>("path_tracer", descriptorPool.pool);

}

void PathTracer::createDescriptorSetLayouts() {
    raytrace.descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("ray_trace")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_RAYGEN_BIT_KHR)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_RAYGEN_BIT_KHR)
            .binding(4)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_RAYGEN_BIT_KHR)
        .createLayout();

    const uint32_t numInstances = drawables.size();

    raytrace.instanceDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("raytrace_instance")
            .binding(0) // materials
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(numInstances)
                .shaderStages(ALL_RAY_TRACE_STAGES)
            .binding(1) // material ids
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(numInstances)
                .shaderStages(ALL_RAY_TRACE_STAGES)
            .binding(2) // scene objects
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(ALL_RAY_TRACE_STAGES)
            .binding(3) // rectangles
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(ALL_RAY_TRACE_STAGES)
            .binding(4) // disks
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(ALL_RAY_TRACE_STAGES)
            .binding(5) // spheres
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(ALL_RAY_TRACE_STAGES)
            .binding(6) // light shape ref
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(ALL_RAY_TRACE_STAGES)
            .createLayout();

    raytrace.vertexDescriptorSetLayout =
            device.descriptorSetLayoutBuilder()
                    .name("raytrace_vertex")
                    .binding(0) // vertex buffer binding
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(numInstances)
                    .shaderStages(ALL_RAY_TRACE_STAGES)
                    .binding(1)     // index buffer bindings
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(numInstances)
                    .shaderStages(ALL_RAY_TRACE_STAGES)
                    .binding(2)     // vertex offset buffer
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(numInstances)
                    .shaderStages(ALL_RAY_TRACE_STAGES)
                    .createLayout();

    sceneDescriptorSetLayout = 
        device.descriptorSetLayoutBuilder()
            .name("scene_info")
                .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(ALL_RAY_TRACE_STAGES)
            .createLayout();
    
    envMapDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("environment_map")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(ALL_RAY_TRACE_STAGES)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(ALL_RAY_TRACE_STAGES)
                .immutableSamplers(envMapDistribution.sampler)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(ALL_RAY_TRACE_STAGES)
                .immutableSamplers(envMapDistribution.sampler)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(ALL_RAY_TRACE_STAGES)
                .immutableSamplers(envMapDistribution.sampler)
            .binding(4)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(ALL_RAY_TRACE_STAGES)
                .immutableSamplers(envMapDistribution.sampler)
            .createLayout();

    denoiserGuideSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("denoiser_guide")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_RAYGEN_BIT_KHR)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_RAYGEN_BIT_KHR)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_RAYGEN_BIT_KHR)
            .createLayout();

}

void PathTracer::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( { raytrace.descriptorSetLayout, raytrace.instanceDescriptorSetLayout,
                                           raytrace.vertexDescriptorSetLayout, sceneDescriptorSetLayout, envMapDescriptorSetLayout,
                                           denoiserGuideSetLayout});
    raytrace.descriptorSet = sets[0];
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("raytrace_base", raytrace.descriptorSet);

    raytrace.instanceDescriptorSet = sets[1];
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("raytrace_instance", raytrace.instanceDescriptorSet);

    raytrace.vertexDescriptorSet = sets[2];
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("raytrace_vertex", raytrace.vertexDescriptorSet);
    
    sceneDescriptorSet = sets[3];
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("scene_info", sceneDescriptorSet);
    
    envMapDescriptorSet = sets[4];
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("environment_map", envMapDescriptorSet);
    
    denoiserGuideSet = sets[5];
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("denoiser_guide", envMapDescriptorSet);


    auto writes = initializers::writeDescriptorSets<12>();

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
    VkDescriptorBufferInfo camInfo{ inverseCamProj, 0, VK_WHOLE_SIZE};
    writes[1].pBufferInfo = &camInfo;

    writes[2].dstSet = raytrace.descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo imageInfo{ VK_NULL_HANDLE, rayTracedTexture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &imageInfo;
    
    writes[3].dstSet = raytrace.descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo envMapInfo{environmentMap.sampler.handle, environmentMap.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[3].pImageInfo = &envMapInfo;

    std::vector<VkDescriptorBufferInfo> materialBufferInfo;

    for(auto object : objects){
        VkDescriptorBufferInfo info{object->materialBuffer, 0, VK_WHOLE_SIZE};
        materialBufferInfo.push_back(info);
    }

    writes[4].dstSet = raytrace.instanceDescriptorSet;
    writes[4].dstBinding = 0;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = COUNT(materialBufferInfo);
    writes[4].pBufferInfo = materialBufferInfo.data();

    // instance descriptorSet
    std::vector<VkDescriptorBufferInfo> matIdBufferInfo{};
    for(auto object : objects){
        VkDescriptorBufferInfo info{object->materialIdBuffer, 0, VK_WHOLE_SIZE};
        matIdBufferInfo.push_back(info);
    }

    writes[5].dstSet = raytrace.instanceDescriptorSet;
    writes[5].dstBinding = 1;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = COUNT(matIdBufferInfo);
    writes[5].pBufferInfo = matIdBufferInfo.data();

    VkDescriptorBufferInfo sceneBufferInfo{};
    sceneBufferInfo.buffer = sceneObjectBuffer;
    sceneBufferInfo.offset = 0;
    sceneBufferInfo.range = VK_WHOLE_SIZE;

    writes[6].dstSet= raytrace.instanceDescriptorSet;
    writes[6].dstBinding = 2;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[6].descriptorCount = 1;
    writes[6].pBufferInfo = &sceneBufferInfo;

    // vertex descriptorSet
    std::vector<VkDescriptorBufferInfo> vertexBufferInfo{};
    for(auto object : objects){
        VkDescriptorBufferInfo info{object->vertexBuffer, 0, VK_WHOLE_SIZE};
        vertexBufferInfo.push_back(info);
    }

    writes[7].dstSet = raytrace.vertexDescriptorSet;
    writes[7].dstBinding = 0;
    writes[7].descriptorCount = COUNT(vertexBufferInfo);
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[7].pBufferInfo = vertexBufferInfo.data();

    std::vector<VkDescriptorBufferInfo> indexBufferInfo{};
    for(auto object : objects){
        VkDescriptorBufferInfo info{object->indexBuffer, 0, VK_WHOLE_SIZE};
        indexBufferInfo.push_back(info);
    }

    writes[8].dstSet = raytrace.vertexDescriptorSet;
    writes[8].dstBinding = 1;
    writes[8].descriptorCount = COUNT(indexBufferInfo);
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[8].pBufferInfo = indexBufferInfo.data();

    std::vector<VkDescriptorBufferInfo> offsetBufferInfo{};
    for(auto object : objects){
        VkDescriptorBufferInfo info{object->offsetBuffer, 0, VK_WHOLE_SIZE};
        offsetBufferInfo.push_back(info);
    }

    writes[9].dstSet = raytrace.vertexDescriptorSet;
    writes[9].dstBinding = 2;
    writes[9].descriptorCount = COUNT(offsetBufferInfo);
    writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[9].pBufferInfo = offsetBufferInfo.data();

    writes[10].dstSet = sceneDescriptorSet;
    writes[10].dstBinding = 0;
    writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[10].descriptorCount = 1;
    VkDescriptorBufferInfo lightsInfo{lightsBuffer, 0, VK_WHOLE_SIZE};
    writes[10].pBufferInfo = &lightsInfo;

    writes[11].dstSet = raytrace.descriptorSet;
    writes[11].dstBinding = 4;
    writes[11].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[11].descriptorCount = 1;
    VkDescriptorBufferInfo prevCamInfo{ previousInverseCamProj, 0, VK_WHOLE_SIZE};
    writes[11].pBufferInfo = &prevCamInfo;

    device.updateDescriptorSets(writes);
    
    writes = initializers::writeDescriptorSets<5>();
    
    writes[0].dstSet = envMapDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &envMapInfo;
    
    writes[1].dstSet = envMapDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo cFuncImageInfo{VK_NULL_HANDLE, envMapDistribution.pConditionalVFunc.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[1].pImageInfo = &cFuncImageInfo;

    writes[2].dstSet = envMapDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo cCdfImageInfo{VK_NULL_HANDLE, envMapDistribution.pConditionalVCdf.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[2].pImageInfo = &cCdfImageInfo;

    writes[3].dstSet = envMapDescriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo mFuncImageInfo{VK_NULL_HANDLE, envMapDistribution.pMarginal.func.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[3].pImageInfo = &mFuncImageInfo;

    writes[4].dstSet = envMapDescriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].descriptorCount = 1;
    VkDescriptorImageInfo mCdfImageInfo{VK_NULL_HANDLE, envMapDistribution.pMarginal.cdf.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[4].pImageInfo = &mCdfImageInfo;

    device.updateDescriptorSets(writes);
    
    // update denoiser set
    writes = initializers::writeDescriptorSets<3>();

    writes[0].dstSet = denoiserGuideSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo dgAlbedoInfo{VK_NULL_HANDLE, denoiserGuide.albedo.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[0].pImageInfo = &dgAlbedoInfo;

    writes[1].dstSet = denoiserGuideSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo dgNormalInfo{VK_NULL_HANDLE, denoiserGuide.normal.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[1].pImageInfo = &dgNormalInfo;

    writes[2].dstSet = denoiserGuideSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo dgFlowInfo{VK_NULL_HANDLE, denoiserGuide.flow.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &dgFlowInfo;

    device.updateDescriptorSets(writes);
    
    // shapes 
    writes = initializers::writeDescriptorSets<4>();
    writes[0].dstSet = raytrace.instanceDescriptorSet;
    writes[0].dstBinding = 3;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo rectInfo{ shapes.rectangles, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &rectInfo;
    
    writes[1].dstSet = raytrace.instanceDescriptorSet;
    writes[1].dstBinding = 4;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo diskInfo{ shapes.disks, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &diskInfo;

    writes[2].dstSet = raytrace.instanceDescriptorSet;
    writes[2].dstBinding = 5;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo sphereInfo{ shapes.spheres, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &sphereInfo;

    writes[3].dstSet = raytrace.instanceDescriptorSet;
    writes[3].dstBinding = 6;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    VkDescriptorBufferInfo shapeRefInfo{ lightShapeRef, 0, VK_WHOLE_SIZE };
    writes[3].pBufferInfo = &shapeRefInfo;

    device.updateDescriptorSets(writes);
}

void PathTracer::createCommandPool() {
    static int count = 0;
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount * commandBufferGroups);

    raytraceFinished.resize(swapChainImageCount);

    std::vector<VkSemaphore> semaphores;
    std::vector<VkPipelineStageFlags> stages(swapChainImageCount, VK_PIPELINE_STAGE_TRANSFER_BIT);
    for(int i = 0; i < swapChainImageCount; i++){
        auto index = i * commandBufferGroups;
        spdlog::info("render_{} raytrace_{}", index, index+1);
        device.setName<VK_OBJECT_TYPE_COMMAND_BUFFER>(fmt::format("render_{}", index), commandBuffers[index]);
        device.setName<VK_OBJECT_TYPE_COMMAND_BUFFER>(fmt::format("raytrace_{}", index + 1), commandBuffers[index+1]);
        raytraceFinished[i] = device.createSemaphore();
        device.setName<VK_OBJECT_TYPE_SEMAPHORE>(fmt::format("ray_trace_finished_{}", i), raytraceFinished[i].semaphore);
        semaphores.push_back(raytraceFinished[i]);
    }

    waitSemaphores.push_back(semaphores);
    waitStages.push_back(stages);
}

void PathTracer::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}

void PathTracer::initCanvas() {
    VkPushConstantRange range{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float)};
    auto fragShader = resource("render.frag.spv");
//    std::optional<std::string> fragShader;
    canvas = Canvas{
        this,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        std::nullopt,
        fragShader,
        range
    };
    canvas.init();
    canvas.setConstants(&m.sceneConstants.exposure);
}

void PathTracer::createInverseCam() {
    inverseCamProj = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(glm::mat4) * 3);
    previousInverseCamProj = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(glm::mat4) * 3);
}

void PathTracer::createRayTracingPipeline() {
    auto rayGenShaderModule = device.createShaderModule( resource("raygen.rgen.spv"));
    auto missShaderModule = device.createShaderModule( resource("miss.rmiss.spv"));
    auto lightMissShaderModule = device.createShaderModule( resource("light.rmiss.spv"));

    auto closestHitShaderModule = device.createShaderModule( resource("closesthit.rchit.spv"));
    auto occlusionPrimaryHitShaderModule = device.createShaderModule( resource("occlusion.rchit.spv"));

    auto volumeHitShaderModule = device.createShaderModule( resource("volume.rchit.spv"));
    auto volumeAnyHitShaderModule = device.createShaderModule( resource("volume.rahit.spv"));

    auto volumeOcclusionHitShaderModule = device.createShaderModule(resource("volume_occlusion.rchit.spv"));
    auto volumeOcclusionAnyHitShaderModule = device.createShaderModule(resource("volume_occlusion.rahit.spv"));

    auto glassHitShaderModule = device.createShaderModule( resource("glass.rchit.spv"));
    auto glassOcclusionHitShaderModule = device.createShaderModule( resource("occlusion_glass.rchit.spv"));

    std::vector<ShaderInfo> shaders(eShaderCount);
    shaders[eRayGen] = { rayGenShaderModule, VK_SHADER_STAGE_RAYGEN_BIT_KHR};

    shaders[eMiss] = { missShaderModule, VK_SHADER_STAGE_MISS_BIT_KHR};
    shaders[eLightMiss] = { lightMissShaderModule, VK_SHADER_STAGE_MISS_BIT_KHR};

    shaders[eClosestHit] = { closestHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    shaders[eOcclusionPrimary] = { occlusionPrimaryHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};

    shaders[eVolumeHit] = { volumeHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    shaders[eVolumeAnyHit] = { volumeAnyHitShaderModule, VK_SHADER_STAGE_ANY_HIT_BIT_KHR};

    shaders[eOcclusionVolumeHit] = { volumeOcclusionHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    shaders[eOcclusionVolumeAnyHit] = { volumeOcclusionAnyHitShaderModule, VK_SHADER_STAGE_ANY_HIT_BIT_KHR};

    shaders[eGlassHit] = { glassHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
    shaders[eGlassOcclusion] = { glassOcclusionHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR };
    auto stages = initializers::rayTraceShaderStages(shaders);


    std::vector<VkSpecializationMapEntry> entries(8);
    entries[0].constantID = 0;
    entries[0].offset = 0;
    entries[0].size = sizeof(int);

    entries[1].constantID = 1;
    entries[1].offset = sizeof(int);
    entries[1].size = sizeof(int);

    entries[2].constantID = 2;
    entries[2].offset = sizeof(int) * 2;
    entries[2].size = sizeof(int);

    entries[3].constantID = 3;
    entries[3].offset = sizeof(int) * 3;
    entries[3].size = sizeof(int);

    entries[4].constantID = 4;
    entries[4].offset = sizeof(int) * 4;
    entries[4].size = sizeof(int);

    entries[5].constantID = 5;
    entries[5].offset = sizeof(int) * 5;
    entries[5].size = sizeof(int);

    entries[6].constantID = 6;
    entries[6].offset = sizeof(int) * 6;
    entries[6].size = sizeof(int);

    entries[7].constantID = 7;
    entries[7].offset = sizeof(int) * 7;
    entries[7].size = sizeof(int);

    m.specializationConstants.g2DivideByDenominator =
            int(m.specializationConstants.ndfFunction == int(Ndf::GGX) && m.specializationConstants.useOptimizedG2 == 1);

    VkSpecializationInfo specializationInfo{
        COUNT(entries),
        entries.data(),
        sizeof(m.specializationConstants),
        &m.specializationConstants
    };

    stages[eRayGen].pSpecializationInfo = &specializationInfo;
    stages[eClosestHit].pSpecializationInfo = &specializationInfo;
    stages[eVolumeHit].pSpecializationInfo = &specializationInfo;
    stages[eVolumeAnyHit].pSpecializationInfo = &specializationInfo;
    stages[eGlassHit].pSpecializationInfo = &specializationInfo;
    stages[eGlassOcclusion].pSpecializationInfo = &specializationInfo;
    stages[eOcclusionVolumeAnyHit].pSpecializationInfo = &specializationInfo;


    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
    shaderGroups.push_back(shaderTablesDesc.rayGenGroup(eRayGen));

    shaderGroups.push_back(shaderTablesDesc.addMissGroup(eMiss));
    shaderGroups.push_back(shaderTablesDesc.addMissGroup(eLightMiss));

    shaderGroups.push_back(shaderTablesDesc.addHitGroup(eClosestHit));
    shaderGroups.push_back(shaderTablesDesc.addHitGroup(eVolumeHit, VK_SHADER_UNUSED_KHR, eVolumeAnyHit));
    shaderGroups.push_back(shaderTablesDesc.addHitGroup(eGlassHit));

    shaderGroups.push_back(shaderTablesDesc.addHitGroup(eOcclusionPrimary));
    shaderGroups.push_back(shaderTablesDesc.addHitGroup(eOcclusionVolumeHit, VK_SHADER_UNUSED_KHR, eOcclusionVolumeAnyHit));
    shaderGroups.push_back(shaderTablesDesc.addHitGroup(eOcclusionPrimary));

    shaderTablesDesc.hitGroups[static_cast<int>(HitShaders::volume)].addRecord(device.getAddress(mediumBuffer));
    shaderTablesDesc.hitGroups[static_cast<int>(HitShaders::occlusionVolume)].addRecord(device.getAddress(mediumBuffer));

    dispose(raytrace.layout);
    raytrace.layout = device.createPipelineLayout({ raytrace.descriptorSetLayout, raytrace.instanceDescriptorSetLayout
                                                    , raytrace.vertexDescriptorSetLayout, sceneDescriptorSetLayout,
                                                    envMapDescriptorSetLayout, denoiserGuideSetLayout }
                                                    , {{ALL_RAY_TRACE_STAGES, 0, sizeof(m.sceneConstants)}});
    VkRayTracingPipelineCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
    createInfo.stageCount = COUNT(stages);
    createInfo.pStages = stages.data();
    createInfo.groupCount = COUNT(shaderGroups);
    createInfo.pGroups = shaderGroups.data();
    createInfo.maxPipelineRayRecursionDepth = 31;
    createInfo.layout = raytrace.layout.handle;

    raytrace.pipeline = device.createRayTracingPipeline(createInfo);
    bindingTables = shaderTablesDesc.compile(device, raytrace.pipeline);
}

void PathTracer::createPostProcessPipeline() {
    auto module = device.createShaderModule(resource("post_process.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    postProcess.layout = device.createPipelineLayout({ raytrace.descriptorSetLayout }, { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float)} });

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = postProcess.layout.handle;

    postProcess.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
}

void PathTracer::rayTrace(VkCommandBuffer commandBuffer) {
//    m.sceneConstants.mask = m.sceneConstants.mask & ~static_cast<uint32_t>(eLights);
    if(m.sceneConstants.adaptiveSampling == 0){
        m.sceneConstants.numSamples = glm::clamp(m.sceneConstants.numSamples, 1u, 100u);
    }
    accelerationStructureBuildBarrier(commandBuffer);
    std::vector<VkDescriptorSet> sets{ raytrace.descriptorSet, raytrace.instanceDescriptorSet, raytrace.vertexDescriptorSet, sceneDescriptorSet, envMapDescriptorSet, denoiserGuideSet  };
    assert(raytrace.pipeline);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytrace.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytrace.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, raytrace.layout.handle, ALL_RAY_TRACE_STAGES, 0, sizeof(m.sceneConstants), &m.sceneConstants);
    vkCmdTraceRaysKHR(commandBuffer, bindingTables.rayGen, bindingTables.miss, bindingTables.closestHit,
                      bindingTables.callable, swapChain.extent.width, swapChain.extent.height, 1);

    if(!shouldDenoise){
        transferImage(commandBuffer);
    }

}

void PathTracer::denoise() {
    auto commandBuffer = commandBuffers[currentImageIndex * commandBufferGroups + 2];
    auto beginInfo = initializers::commandBufferBeginInfo();

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    denoiser->update(commandBuffer,
                     rayTracedTexture.image,
                     denoiserGuide.albedo.image,
                     denoiserGuide.normal.image);
    vkEndCommandBuffer(commandBuffer);

    auto waitValue = fenceValue;
    denoiseTimelineInfo.waitSemaphoreValueCount = 1;
    denoiseTimelineInfo.pWaitSemaphoreValues = &waitValue;
    denoiseTimelineInfo.signalSemaphoreValueCount = 1;
    fenceValue++;
    denoiseTimelineInfo.pSignalSemaphoreValues = &fenceValue;

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.pNext = &denoiseTimelineInfo;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = denoiseSemaphore.vk;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(device.queues.graphics, 1, &submitInfo, VK_NULL_HANDLE);

    // Wait for Vulkan to copy image to denoise buffers
    cudaExternalSemaphoreWaitParams waitParams{};
    waitParams.flags = 0;
    waitParams.params.fence.value = fenceValue;
    cudaWaitExternalSemaphoresAsync(&denoiseSemaphore.cu, &waitParams, 1, nullptr);
    denoiser->exec();

    cudaExternalSemaphoreSignalParams signalParams{};
    signalParams.flags = 0;
    signalParams.params.fence.value = ++fenceValue;
    cudaSignalExternalSemaphoresAsync(&denoiseSemaphore.cu, &signalParams, 1, optix->m_cudaStream);


    commandBuffer = commandBuffers[currentImageIndex * commandBufferGroups + 3];
    beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    denoiser->copyOutputTo(commandBuffer, rayTracedTexture.image);
    transferImage(commandBuffer);
    vkEndCommandBuffer(commandBuffer);

    denoiseTimelineInfo.waitSemaphoreValueCount = 1;
    denoiseTimelineInfo.signalSemaphoreValueCount = 0;
    denoiseTimelineInfo.pWaitSemaphoreValues = &fenceValue;

    submitInfo.pNext = &denoiseTimelineInfo;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = denoiseSemaphore.vk;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = raytraceFinished[currentImageIndex];
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(device.queues.graphics, 1, &submitInfo, VK_NULL_HANDLE);
}

void PathTracer::transferImage(VkCommandBuffer commandBuffer) {
    rayTraceToTransferBarrier(commandBuffer);
    VkImageCopy copy{};
    copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.srcSubresource.mipLevel = 0;
    copy.srcSubresource.baseArrayLayer = 0;
    copy.srcSubresource.layerCount = 1;
    copy.srcOffset = {0, 0, 0};

    copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.dstSubresource.mipLevel = 0;
    copy.dstSubresource.baseArrayLayer = 0;
    copy.dstSubresource.layerCount = 1;
    copy.dstOffset = {0, 0, 0};
    copy.extent = {swapChain.width(), swapChain.height(), 1};

    vkCmdCopyImage(commandBuffer, rayTracedTexture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            , canvas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    if(gui.takeScreenShot){
        VkBufferImageCopy region{
                0, 0, 0,
                {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                {0, 0, 0},
                {swapChain.width(), swapChain.height(), 1}
        };
        vkCmdCopyImageToBuffer(commandBuffer, rayTracedTexture.image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, denoiser->data().color.buf,
                               1, &region);

        vkCmdCopyImageToBuffer(commandBuffer, denoiserGuide.albedo.image,
                               VK_IMAGE_LAYOUT_GENERAL, denoiser->data().albedo.buf,
                               1, &region);

        vkCmdCopyImageToBuffer(commandBuffer, denoiserGuide.normal.image,
                               VK_IMAGE_LAYOUT_GENERAL, denoiser->data().normal.buf,
                               1, &region);


    }
    transferToRenderBarrier(commandBuffer);
}

void PathTracer::rayTraceToTransferBarrier(VkCommandBuffer commandBuffer) const {
    std::vector<VkImageMemoryBarrier> barriers(2);
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].srcAccessMask = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].image = rayTracedTexture.image;
    barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].subresourceRange.baseArrayLayer = 0;
    barriers[0].subresourceRange.baseMipLevel = 0;
    barriers[0].subresourceRange.layerCount = 1;
    barriers[0].subresourceRange.levelCount = 1;

    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].image = canvas.image;
    barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[1].subresourceRange.baseArrayLayer = 0;
    barriers[1].subresourceRange.baseMipLevel = 0;
    barriers[1].subresourceRange.layerCount = 1;
    barriers[1].subresourceRange.levelCount = 1;



    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,

                         0,
                         VK_NULL_HANDLE,
                         0,
                         VK_NULL_HANDLE,
                         COUNT(barriers),
                         barriers.data());
}

void PathTracer::transferToRenderBarrier(VkCommandBuffer commandBuffer) const {
    std::vector<VkImageMemoryBarrier> barriers(2);
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].dstAccessMask = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].image = rayTracedTexture.image;
    barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].subresourceRange.baseArrayLayer = 0;
    barriers[0].subresourceRange.baseMipLevel = 0;
    barriers[0].subresourceRange.layerCount = 1;
    barriers[0].subresourceRange.levelCount = 1;

    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].image = canvas.image;
    barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[1].subresourceRange.baseArrayLayer = 0;
    barriers[1].subresourceRange.baseMipLevel = 0;
    barriers[1].subresourceRange.layerCount = 1;
    barriers[1].subresourceRange.levelCount = 1;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         0,
                         0,
                         VK_NULL_HANDLE,
                         0,
                         VK_NULL_HANDLE,
                         COUNT(barriers),
                         barriers.data());
}



void PathTracer::onSwapChainDispose() {
    dispose(raytrace.pipeline);
}

void PathTracer::onSwapChainRecreation() {
    m.sceneConstants.currentSample = 0;
    camera->onResize(width, height);
    initDenoiser();
    initCanvas();

    // FIXME memory barrier required, between this and raytrace stage
    clearAccelerationStructure();
    createAccelerationStructure(instances);

    createDescriptorSetLayouts();
    updateDescriptorSets();
    createRayTracingPipeline();
    createPostProcessPipeline();
}

VkCommandBuffer *PathTracer::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex * commandBufferGroups];

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

    canvas.draw(commandBuffer);
    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

//    rayTrace(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void PathTracer::renderUI(VkCommandBuffer commandBuffer) {
   gui.render(commandBuffer);
}

void PathTracer::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }

    m.sceneConstants.frame++;
    auto cam = camera->cam();
    inverseCamProj.map<glm::mat4>([&](auto ptr){
        auto view = glm::inverse(cam.view);
        auto proj = glm::inverse(cam.proj);
        auto viewProjection = proj * view;
        *ptr = view;
        *(ptr+1) = proj;
        *(ptr+2) = viewProjection;
    });

//    auto lights = reinterpret_cast<Light*>(lightsBuffer.map());
//    lights[1].normal = camera->viewDir;
//    lights[1].position = camera->position();
//    lightsBuffer.unmap();

    m.fps = framePerSecond;
}

void PathTracer::newFrame(){
    camera->newFrame();

    auto commandBuffer = commandBuffers[currentImageIndex * commandBufferGroups + 1];
    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    rayTrace(commandBuffer);
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };

    if(!shouldDenoise) {
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = raytraceFinished[currentImageIndex];
    }else{
        fenceValue++;
        denoiseTimelineInfo.waitSemaphoreValueCount = 0;
        denoiseTimelineInfo.signalSemaphoreValueCount = 1;
        denoiseTimelineInfo.pSignalSemaphoreValues = &fenceValue;
        submitInfo.pNext = &denoiseTimelineInfo;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = denoiseSemaphore.vk;
    }

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(device.queues.graphics, 1, &submitInfo, VK_NULL_HANDLE);

    if(shouldDenoise){
        denoise();
    }

}

void PathTracer::endFrame() {
    shouldDenoise = m.denoise && m.sceneConstants.adaptiveSampling != 1;

    if(m.sceneConstants.adaptiveSampling == 1) {
        m.sceneConstants.currentSample =
                glm::clamp(m.sceneConstants.currentSample, 0u, m.sceneConstants.numSamples - 1);
        m.sceneConstants.currentSample++;
        shouldDenoise = m.denoise && ((m.sceneConstants.currentSample + 1) % denoiseAfterFrames) == 0;

        if(m.denoise && m.sceneConstants.currentSample >= m.sceneConstants.numSamples){
            m.denoise = false;
            shouldDenoise = true;
        }

        if (camera->moved()) {
            m.sceneConstants.currentSample = 0;
        }
    }
    gui.endFrame();

    static std::once_flag dragLoadFlag;

    if(dragonLoad.wait_for(chrono::milliseconds(0)) == std::future_status::ready){
        std::call_once(dragLoadFlag, [&]{
            rt::MeshObjectInstance dragInstance{};
            dragInstance.object = rt::TriangleMesh{ &drawables["dragon"] };
            dragInstance.object.metaData[0].hitGroupId = 0;
            dragInstance.object.metaData[0].mask = eDragon;
            float y = drawables["cornell"].bounds.min.y - drawables["dragon"].bounds.min.y;
            dragInstance.xform = glm::translate(glm::mat4{1}, { 0, y, 0});
            instances.push_back(dragInstance);
            objects.push_back(&drawables["dragon"]);
            m.dragonMaterial = reinterpret_cast<Material*>(objects[2]->materialBuffer.map());
            m.dragonMaterial->emission = glm::vec3(0);
            m.dragonMaterial->roughness = 0.5;
            m.dragonMaterial->metalness = glm::vec3(0);
            m.dragonMaterial->opacity = 10;

            m.dragonReady = true;
            spdlog::info("dragon loaded");
            m.sceneConstants.currentSample = 0;
            invalidateSwapChain();
        });
    }

    if(shouldDenoise){
        spdlog::debug("applying denoiser on sample {}", m.sceneConstants.currentSample);
    }

    static int saveId = 0;
    if(gui.takeScreenShot){
        gui.takeScreenShot = false;
        threadPool.async([&] {
            auto path = fmt::format("c:/temp/path_traced_image_{}.hdr", saveId);
            auto albedoPath = fmt::format("c:/temp/path_traced_image_albedo_{}.hdr", saveId);
            auto normalPath = fmt::format("c:/temp/path_traced_image_normal_{}.hdr", saveId);
            textures::save(device, rayTracedTexture, FileFormat::HDR, path);
            textures::save(device, denoiserGuide.albedo, FileFormat::HDR, albedoPath);
            textures::save(device, denoiserGuide.normal, FileFormat::HDR, normalPath);
            spdlog::info("image saved to {}", path);
            saveId++;
        });
    }

    auto cam = camera->cam();
    previousInverseCamProj.map<glm::mat4>([&](auto ptr){
        auto view = glm::inverse(cam.view);
        auto proj = glm::inverse(cam.proj);
        auto viewProjection = proj * view;
        *ptr = view;
        *(ptr+1) = proj;
        *(ptr+2) = viewProjection;
    });
}

void PathTracer::checkAppInputs() {
    camera->processInput();
}

void PathTracer::cleanup() {
    dispose(denoiser);
    dispose(optix);
    for(auto& [_, drawable] : drawables){
        drawable.materialBuffer.unmap();
    }
    lightsBuffer.unmap();
    threadPool.shutdown();
    mediumBuffer.unmap();
}

void PathTracer::onPause() {
    VulkanBaseApp::onPause();
}

#include "denoiser_test.hpp"

int main(){
//    testCudaInterop();
    try{

        Settings settings;
        settings.width = 2048;
        settings.height = 1080;
        settings.depthTest = true;
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;

        settings.instanceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
        settings.instanceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);

        settings.deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);

#ifdef WIN32
    settings.deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
    settings.deviceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
#endif

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = PathTracer{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}