#include "PhotonMapping.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "neighbour_search.h"
#include "kdtree.hpp"

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
    initKdTree();
    initDebugData();
    initRayTraceInfo();
    initPhotonMapData();
    loadLTC();
    initLight();
    loadModel();
    initCamera();
    initCanvas();
    initGBuffer();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    updateRtDescriptorSets();
    updateGBufferDescriptorSet();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
    createRayTracingPipeline();
//    generatePhotons();
}
void PhotonMapping::initKdTree() {

}

void PhotonMapping::balanceKdTree() {
    using BalanceFunc = std::function<void(std::vector<Photon*>&, std::span<Photon>, int, int)>;

    BalanceFunc balance = [&](std::vector<Photon*>& tree, std::span<Photon> photons, int index, int numPhotons){
        if(photons.empty()){
            if(index < numPhotons) {
                tree[index] = nullptr;
            }
            return;
        }


        glm::vec3 bMin{std::numeric_limits<float>::max()};
        glm::vec3 bMax{std::numeric_limits<float>::min()};

        std::for_each(photons.begin(), photons.end(), [&](const Photon& photon){
            bMin = glm::min(photon.position.xyz(), bMin);
            bMax = glm::max(photon.position.xyz(), bMax);
        });

        auto dim = bMax - bMin;
        int axis = dim.z > dim.y ? 2 : (dim.y > dim.x ? 1 : 0);

        std::sort(photons.begin(), photons.end(), [axis](const Photon& a, const Photon& b){
            return a.position[axis] < b.position[axis];
        });

        auto middleIndex = photons.size()/2;
        Photon& middle = photons[middleIndex];
        middle.axis = middleIndex > 0 ? axis : -1;

        tree[index] = &middle;


        balance(tree, {photons.data(), middleIndex },  2 * index + 1, numPhotons);
        const auto offset = middleIndex + 1;
        balance(tree, {photons.data() + offset, photons.size() - offset }, 2 * index + 2, numPhotons);
    };

    photonsReady = false;
    auto n = std::ceil(std::log2(stats.photonCount));
    auto size = size_t(std::pow(2, n));
    size = stats.photonCount < size ? size - 1 : size;
    std::vector<Photon*> tree(size);

    spdlog::info("balancing generated photons");
    std::span<Photon> photons{ reinterpret_cast<Photon*>(mStagingBuffer.map()), static_cast<size_t>(stats.photonCount) };
    balance(tree, photons, 0, size );

    static std::vector<int> treeIndex;
    treeIndex.resize(kdTree.gpu.sizeAs<int>());
    std::generate(treeIndex.begin(), treeIndex.end(), []{ return -1; });
    for(int i = 0; i < size; i++){
        auto itr = std::find_if(photons.begin(), photons.end(), [&](auto& point){ return  tree[i] == &point; });
        treeIndex[i] = itr != photons.end() ? std::distance(photons.begin(), itr) : -1;
    }



    stats.treeSize = size;
    mStagingBuffer.unmap();
    spdlog::info("balancing complete");


    onIdle([&]{
        kdTree.gpu.copy(treeIndex);
        device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT};
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
            VkBufferCopy region{0, 0, mStagingBuffer.size};
            vkCmdCopyBuffer(commandBuffer, mStagingBuffer, generatedPhotons, 1, &region);

            vkCmdUpdateBuffer(commandBuffer, photonStats, 0, sizeof(stats), &stats);
        });
        spdlog::info("balanced photon map transferred to gpu");

        auto tree = reinterpret_cast<int*>(kdTree.gpu.map());
        for(int i = stats.treeSize; i < stats.treeSize+10; i++){
            spdlog::info(":{} => {}", i, tree[i]);
        }
        kdTree.gpu.unmap();
        photonsReady = true;
//        computeIR.run = true;
//        computeIndirectRadianceCPU();
    });
}

void PhotonMapping::initDebugData() {
    debugInfo.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(DebugData), "debug_info");
    debugInfo.cpu = reinterpret_cast<DebugData*>(debugInfo.gpu.map());
    *debugInfo.cpu = DebugData{ .numNeighbours = numNeighbours};
    scratchBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, (100 << 20) );
}

void PhotonMapping::initPhotonMapData() {
    photonMapInfo = PhotonMapInfo{rayTraceInfo.cpu->mask, 5, 20000, 60, 0.25};

    std::vector<char> allocation(sizeof(Photon) * MaxPhotons);
    generatedPhotons = device.createCpuVisibleBuffer(allocation.data(), allocation.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    allocation.resize(MaxPhotons * sizeof(int));
    kdTree.gpu = device.createCpuVisibleBuffer(allocation.data(), allocation.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    PhotonStats ps{};
    photonStats = device.createDeviceLocalBuffer(&ps, sizeof(ps), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    allocation.resize(MaxPhotons * sizeof(int));
    nearestNeighbourBuffer = device.createCpuVisibleBuffer(allocation.data(), allocation.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    *(reinterpret_cast<int*>(nearestNeighbourBuffer.map())) = -1;
}

void PhotonMapping::loadModel() {
    auto cornellBox = primitives::cornellBox();

    for(auto& mesh : cornellBox) {
        for(auto& vertex : mesh.vertices) {
            vertex.position  = glm::vec4(vertex.position.xyz() * 0.1f, 1);
        }
    }

    auto dim = light.cpu->upperCorner - light.cpu->lowerCorner;
    auto area = length(dim);

    meshes = std::vector<mesh::Mesh>(10);
    meshes[0].name = "Light";
    meshes[0].vertices = cornellBox[0].vertices;
    meshes[0].indices = cornellBox[0].indices;
    meshes[0].material.name = "Light";
    meshes[0].material.ambient = glm::vec3(0);
    meshes[0].material.diffuse = glm::vec3(0);
    meshes[0].material.specular = glm::vec3(0);
    meshes[0].material.emission = light.cpu->power/(area * glm::pi<float>());
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
    spdlog::info("front sphere position: {}", center);
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
    instance.object.metaData[2].mask = ObjectMask::Ceiling;

    instance.object.metaData[6].mask = ObjectMask::Box;
//    instance.object.metaData[6].hitGroupId = HitGroups::Glass;

    instance.object.metaData[7].mask = ObjectMask::Box;
//    instance.object.metaData[7].hitGroupId = HitGroups::Mirror;

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

    LightData ld{};
    ld.position = (lowerCorner + upperCorner) * 0.5f;
    ld.normal = glm::vec4(mesh.vertices.front().normal, 0);
    ld.tangent = glm::vec4(mesh.vertices.front().tangent, 0);
    ld.bitangent = glm::vec4(mesh.vertices.front().bitangent, 0);
    ld.lowerCorner = lowerCorner;
    ld.upperCorner = upperCorner;
    ld.power = glm::vec4(power, 0);

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
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_RAYGEN_BIT_KHR)
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
            .createLayout();


    debugDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("debug_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages( VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages( VK_SHADER_STAGE_ALL)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages( VK_SHADER_STAGE_ALL)
            .createLayout();


    gBuffer.setLayout =
        device.descriptorSetLayoutBuilder()
            .name("g_buffer_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages( VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages( VK_SHADER_STAGE_ALL)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages( VK_SHADER_STAGE_ALL)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages( VK_SHADER_STAGE_ALL)
            .createLayout();


}

void PhotonMapping::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( { lightDescriptorSetLayout, debugDescriptorSetLayout });
    lightDescriptorSet = sets[0];
    debugDescriptorSet = sets[1];


    auto writes = initializers::writeDescriptorSets<7>();

    writes[0].dstSet = lightDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo lightInfo{light.gpu, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &lightInfo;

    writes[1].dstSet = lightDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo photonInfo{generatedPhotons, 0, VK_WHOLE_SIZE};
    writes[1].pBufferInfo = &photonInfo;

    writes[2].dstSet = lightDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo photonStatsInfo{photonStats, 0, VK_WHOLE_SIZE};
    writes[2].pBufferInfo = &photonStatsInfo;

    writes[3].dstSet = lightDescriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    VkDescriptorBufferInfo treeInfo{ kdTree.gpu, 0, VK_WHOLE_SIZE};
    writes[3].pBufferInfo = &treeInfo;

    writes[4].dstSet = debugDescriptorSet;
    writes[4].dstBinding = 0;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    VkDescriptorBufferInfo debugBufferInfo{debugInfo.gpu, 0, VK_WHOLE_SIZE};
    writes[4].pBufferInfo = &debugBufferInfo;

    writes[5].dstSet = debugDescriptorSet;
    writes[5].dstBinding = 1;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = 1;
    VkDescriptorBufferInfo nnInfo{nearestNeighbourBuffer, 0, VK_WHOLE_SIZE};
    writes[5].pBufferInfo = &nnInfo;

    writes[6].dstSet = debugDescriptorSet;
    writes[6].dstBinding = 2;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[6].descriptorCount = 1;
    VkDescriptorBufferInfo scratchInfo{scratchBuffer, 0, VK_WHOLE_SIZE};
    writes[6].pBufferInfo = &scratchInfo;

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

void PhotonMapping::updateGBufferDescriptorSet() {
    gBuffer.descriptorSet = descriptorPool.allocate( { gBuffer.setLayout }).front();

    auto writes = initializers::writeDescriptorSets<4>();

    writes[0].dstSet = gBuffer.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo colorInfo{VK_NULL_HANDLE, gBuffer.color.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[0].pImageInfo = &colorInfo;

    writes[1].dstSet = gBuffer.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo positionInfo{VK_NULL_HANDLE, gBuffer.position.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[1].pImageInfo = &positionInfo;

    writes[2].dstSet = gBuffer.descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo normalInfo{VK_NULL_HANDLE, gBuffer.normal.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &normalInfo;

    writes[3].dstSet = gBuffer.descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo ILInfo{VK_NULL_HANDLE, gBuffer.indirectLight.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[3].pImageInfo = &ILInfo;

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

void PhotonMapping::initGBuffer() {
    textures::create(device, gBuffer.color, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});
    textures::create(device, gBuffer.position, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});
    textures::create(device, gBuffer.normal, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});
    textures::create(device, gBuffer.indirectLight, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});

    gBuffer.color.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    gBuffer.position.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    gBuffer.normal.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    gBuffer.indirectLight.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
}

void PhotonMapping::initRayTraceInfo() {
    rayTraceInfo.gpu = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(RayTraceData));
    rayTraceInfo.cpu = reinterpret_cast<RayTraceData*>(rayTraceInfo.gpu.map());
    rayTraceInfo.cpu->mask = ObjectMask::All & ~ObjectMask::Box;
}

void PhotonMapping::createRayTracingPipeline() {
    auto rayGenShaderModule = device.createShaderModule( resource("raygen.rgen.spv"));
    auto photonRayGenShaderModule = device.createShaderModule( resource("photons.rgen.spv"));
    auto debugRayGenShaderModule = device.createShaderModule( resource("debug.rgen.spv"));

    auto mainClosestHitShaderModule = device.createShaderModule(resource("main.rchit.spv"));
    auto mirrorClosestHitShaderModule = device.createShaderModule(resource("mirror.rchit.spv"));
    auto glassClosestHitShaderModule = device.createShaderModule(resource("glass.rchit.spv"));

    auto photonDiffuseHitShaderModule = device.createShaderModule(resource("photon_diffuse.rchit.spv"));
    auto photonMirrorHitShaderModule = device.createShaderModule(resource("photon_mirror.rchit.spv"));
    auto photonGlassHitShaderModule = device.createShaderModule(resource("photon_glass.rchit.spv"));

    auto debugClosetHitShaderModule = device.createShaderModule(resource("debug.rchit.spv"));

    auto mainMissShaderModule = device.createShaderModule(resource("main.rmiss.spv"));
    auto shadowMissShaderModule = device.createShaderModule(resource("shadow.rmiss.spv"));
    auto photonMissShaderModule = device.createShaderModule(resource("photon_miss.rmiss.spv"));
    auto debugMissShaderModule = device.createShaderModule(resource("debug.rmiss.spv"));

    std::vector<ShaderInfo> shaders(ShaderCount);

    shaders[RayGen] = { rayGenShaderModule, VK_SHADER_STAGE_RAYGEN_BIT_KHR};
    shaders[PhotonRayGen] = { photonRayGenShaderModule, VK_SHADER_STAGE_RAYGEN_BIT_KHR};
    shaders[DebugRayGen] = { debugRayGenShaderModule, VK_SHADER_STAGE_RAYGEN_BIT_KHR};

    shaders[MainMiss] = { mainMissShaderModule, VK_SHADER_STAGE_MISS_BIT_KHR};
    shaders[LightMiss] = { shadowMissShaderModule, VK_SHADER_STAGE_MISS_BIT_KHR};
    shaders[PhotonMiss] = { photonMissShaderModule, VK_SHADER_STAGE_MISS_BIT_KHR};
    shaders[DebugMiss] = { debugMissShaderModule, VK_SHADER_STAGE_MISS_BIT_KHR};

    shaders[MainClosestHit] = { mainClosestHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    shaders[MirrorClosestHit] = { mirrorClosestHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    shaders[GlassClosestHit] = { glassClosestHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};

    shaders[PhotonDiffuseHit] = { photonDiffuseHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    shaders[PhotonMirrorHit] = { photonMirrorHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    shaders[PhotonGlassHit] = { photonGlassHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};

    shaders[DebugClosestHit] = { debugClosetHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
    shaderGroups.push_back(shaderTablesDesc.rayGenGroup(RayGen));
    shaderGroups.push_back(shaderTablesDesc.rayGenGroup("photon_gen", PhotonRayGen));
    shaderGroups.push_back(shaderTablesDesc.rayGenGroup("debug_rgen", DebugRayGen));

    shaderGroups.push_back(shaderTablesDesc.addMissGroup(MainMiss));
    shaderGroups.push_back(shaderTablesDesc.addMissGroup(LightMiss));
    shaderGroups.push_back(shaderTablesDesc.addMissGroup(PhotonMiss));
    shaderGroups.push_back(shaderTablesDesc.addMissGroup(DebugMiss));

    shaderGroups.push_back(shaderTablesDesc.addHitGroup(MainClosestHit));
    shaderGroups.push_back(shaderTablesDesc.addHitGroup(MirrorClosestHit));
    shaderGroups.push_back(shaderTablesDesc.addHitGroup(GlassClosestHit));

    shaderGroups.push_back(shaderTablesDesc.addHitGroup(PhotonDiffuseHit));
    shaderGroups.push_back(shaderTablesDesc.addHitGroup(PhotonMirrorHit));
    shaderGroups.push_back(shaderTablesDesc.addHitGroup(PhotonGlassHit));


    shaderGroups.push_back(shaderTablesDesc.addHitGroup(DebugClosestHit));
    shaderGroups.push_back(shaderTablesDesc.addHitGroup(DebugClosestHit));
    shaderGroups.push_back(shaderTablesDesc.addHitGroup(DebugClosestHit));


    dispose(raytrace.layout);

    raytrace.layout = device.createPipelineLayout({
        raytrace.descriptorSetLayout,
        raytrace.instanceDescriptorSetLayout,
        raytrace.vertexDescriptorSetLayout,
        lightDescriptorSetLayout,
        debugDescriptorSetLayout,
        gBuffer.setLayout
    },
     { {VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(PhotonMapInfo)} });

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

void PhotonMapping::generatePhotons() {
    
    VulkanBuffer cpuBuffer = device.createStagingBuffer(sizeof(PhotonStats));
    mStagingBuffer = device.createStagingBuffer(generatedPhotons.size);
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        photonMapInfo.mask = rayTraceInfo.cpu->mask & ~ObjectMask::Lights;

        std::vector<VkDescriptorSet> sets{
                raytrace.descriptorSet,
                raytrace.instanceDescriptorSet,
                raytrace.vertexDescriptorSet,
                lightDescriptorSet,
                debugDescriptorSet,
                gBuffer.descriptorSet
        };
        assert(raytrace.pipeline);
        vkCmdFillBuffer(commandBuffer, generatedPhotons, 0, generatedPhotons.size, 0u);
        vkCmdFillBuffer(commandBuffer, photonStats, 0, photonStats.size, 0u);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytrace.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytrace.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);

        vkCmdPushConstants(commandBuffer, raytrace.layout.handle, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(PhotonMapInfo), &photonMapInfo);

        vkCmdTraceRaysKHR(commandBuffer, bindingTables.rayGens["photon_gen"], bindingTables.miss, bindingTables.closestHit,
                          bindingTables.callable, photonMapInfo.numPhotons, 1, 1);

        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_HOST_BIT
                             , 0, 1, &barrier, 0, nullptr, 0, nullptr);

        VkBufferCopy region{0, 0, photonStats.size};
        vkCmdCopyBuffer(commandBuffer, photonStats, cpuBuffer, 1, &region);

        region.size = generatedPhotons.size;
        vkCmdCopyBuffer(commandBuffer, generatedPhotons, mStagingBuffer, 1, &region);
    });

    stats = *reinterpret_cast<PhotonStats*>(cpuBuffer.map());
    spdlog::info("Generated {} photons", stats.photonCount);

}

void PhotonMapping::rayTrace(VkCommandBuffer commandBuffer) {
    CanvasToRayTraceBarrier(commandBuffer);

    std::vector<VkDescriptorSet> sets{
        raytrace.descriptorSet,
        raytrace.instanceDescriptorSet,
        raytrace.vertexDescriptorSet,
        lightDescriptorSet,
        debugDescriptorSet,
        gBuffer.descriptorSet
    };
    assert(raytrace.pipeline);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytrace.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytrace.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);

    vkCmdPushConstants(commandBuffer, raytrace.layout.handle, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(PhotonMapInfo), &photonMapInfo);

    vkCmdTraceRaysKHR(commandBuffer, bindingTables.rayGen, bindingTables.miss, bindingTables.closestHit,
                      bindingTables.callable, swapChain.extent.width, swapChain.extent.height, 1);

    rayTraceToCanvasBarrier(commandBuffer);
}

void PhotonMapping::renderPhotons(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    std::array<VkDescriptorSet, 2> sets{};
    sets[0] = lightDescriptorSet;
    sets[1] = debugDescriptorSet;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    camera->push(commandBuffer, render.layout);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, sets.size(), sets.data(), 0, nullptr);
    vkCmdDraw(commandBuffer, 1, stats.photonCount, 0, 0);
}

void PhotonMapping::renderDebug(VkCommandBuffer commandBuffer) {
    if(!debug.enabled) return;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debug.pipeline.handle);
    camera->push(commandBuffer, debug.layout, VK_SHADER_STAGE_GEOMETRY_BIT);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debug.layout.handle, 0, 1, &debugDescriptorSet, 0, nullptr);
    vkCmdDraw(commandBuffer, 1, 1, 0, 0);
    debugInfo.cpu->searchMode = 1;
    renderPhotons(commandBuffer);
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
                .vertexInputState().clear()
                .inputAssemblyState()
                    .points()
                .shaderStage()
                    .vertexShader(resource("render.vert.spv"))
                    .fragmentShader(resource("render.frag.spv"))
                .rasterizationState()
                    .cullNone()
                .layout().clear()
                    .addPushConstantRange(Camera::pushConstant())
                    .addDescriptorSetLayout(lightDescriptorSetLayout)
                    .addDescriptorSetLayout(debugDescriptorSetLayout)
                .name("render")
                .build(render.layout);

        debug.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("debug.vert.spv"))
                    .geometryShader(resource("debug.geom.spv"))
                    .fragmentShader(resource("debug.frag.spv"))
                .rasterizationState()
                    .lineWidth(2.5)
                .depthStencilState()
                    .compareOpAlways()
                .layout().clear()
                    .addPushConstantRange(Camera::pushConstant(VK_SHADER_STAGE_GEOMETRY_BIT))
                    .addDescriptorSetLayout(debugDescriptorSetLayout)
                .name("debug")
            .build(debug.layout);
    //    @formatter:on
}

void PhotonMapping::createComputePipeline() {
    auto module = device.createShaderModule(resource("neighbour_search.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    searchCompute.layout = device.createPipelineLayout( { lightDescriptorSetLayout, debugDescriptorSetLayout });

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = searchCompute.layout.handle;

    searchCompute.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);

    device.setName<VK_OBJECT_TYPE_PIPELINE>("compute_nearest_neighbour", searchCompute.pipeline.handle);


    // Compute indirect radiance shader
    module = device.createShaderModule(resource("compute_indirect_radiance.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeIR.layout = device.createPipelineLayout(
            { gBuffer.setLayout, lightDescriptorSetLayout },
            { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(computeIR.constants)} }
    );

    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = computeIR.layout.handle;

    computeIR.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);

    device.setName<VK_OBJECT_TYPE_PIPELINE>("compute_indirect_radiance", computeIR.pipeline.handle);
}


void PhotonMapping::onSwapChainDispose() {
    dispose(render.pipeline);
    dispose(searchCompute.pipeline);
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
    if(showPhotons && !inspect){
        debugInfo.cpu->searchMode = 0;
        renderPhotons(commandBuffer);
    }else {
        canvas.draw(commandBuffer);
    }

    if(inspect && !showPhotons) {
        renderDebug(commandBuffer);
    }


    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    rayTrace(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void PhotonMapping::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Photon mapping");
    ImGui::SetWindowSize({0, 0});

    ImGui::Text("Photons");
    ImGui::Indent(16);
    static int numPhotons = photonMapInfo.numPhotons;
    ImGui::SliderInt("Num photons", &numPhotons, 1000, MaxPhotons);

    static int maxBounce = photonMapInfo.maxBounces;
    ImGui::SliderInt("Num bounces", &maxBounce, 1, MaxBounces);

    ImGui::SliderFloat("search radius", &photonMapInfo.searchDistance, 0.1, 1);

    int neighbourCount = static_cast<int>(photonMapInfo.neighbourCount);
    ImGui::SliderInt("num photons", &neighbourCount, 0, 1000);
    photonMapInfo.neighbourCount = neighbourCount;

    if(ImGui::Button("generate photons")){
        photonsReady = false;
        generatePhotons();
        runInBackground([&]{ balanceKdTree(); });
    }
    if(photonsReady){
        ImGui::SameLine();
        auto clicked = ImGui::Button("compute IR");
        if(clicked && !computeIR.run) {
            computeIR.run = true;
            computeIndirectRadianceCPU2();
        }
    }

    ImGui::Indent(-16);

    ImGui::Separator();

    ImGui::Text("Debug");
    ImGui::Indent(16);

    if(ImGui::SliderInt("num neighbours", &numNeighbours, 10, 1000)){
        computeNearestNeighbour = true;
    }

    ImGui::SliderFloat("photon size", &debugInfo.cpu->pointSize, 1, 5);


    ImGui::Checkbox("Show photons", &showPhotons);
    ImGui::SameLine();
    ImGui::Checkbox("inspect", &inspect);

    ImGui::Indent(-16);
    ImGui::Separator();

    ImGui::Text("Status:");
    ImGui::Indent(16);
    ImGui::Text("fps: %d", framePerSecond);
    if(inspect) {
        if(selectMode == SelectMode::Off) {
            if(debugInfo.cpu->radius == 0){
                ImGui::Text("right click on surface to inspect");
            }else {
                ImGui::Text("search: \n\tmesh: %s\n\tposition [%.2f, %.2f, %.2f]\n\tradius: %.2f\n\tneighbours %d"
                        , meshes[debugInfo.cpu->meshId].name.c_str()
                        , debugInfo.cpu->hitPosition.x
                        , debugInfo.cpu->hitPosition.y
                        , debugInfo.cpu->hitPosition.z, debugInfo.cpu->radius
                        , debugInfo.cpu->numNeighboursFound);
            }
        }
        if(selectMode == SelectMode::Area) {
            ImGui::Text("drag mouse to expand search area");
        }
    }
    if(computeIR.run){
//        ImGui::Text("indirect radiance compute in progress  %.2f %%",
//                    (computeIR.constants.iteration * 100.f)/ computeIR.numIterations);
        ImGui::Text("IR:  %.2f %%", progress.load());
    }
    ImGui::Indent(-16);


    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);

    photonMapInfo.numPhotons = numPhotons;
    photonMapInfo.maxBounces = maxBounce;
}

void PhotonMapping::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }
    auto cam = camera->cam();
    rayTraceInfo.cpu->viewInverse = glm::inverse(cam.view);
    rayTraceInfo.cpu->projectionInverse = glm::inverse(cam.proj);

    if(camera->moved()) {
        lastCameraUpdate = std::chrono::steady_clock::now();
    }
}

void PhotonMapping::checkAppInputs() {
    if(!ImGui::IsAnyItemActive()) {
        camera->processInput();
    }

    static bool initialPress = true;

    if(inspect && mouse.right.held && initialPress) {
        initialPress = false;
        selectMode = SelectMode::Position;
        debugInfo.cpu->radius = 0;
        debugInfo.cpu->hitPosition = glm::vec3(0);
    }

    if(inspect && mouse.right.released) {
        if(selectMode == SelectMode::Area) {
            computeNearestNeighbour = true;
            selectMode = SelectMode::Off;
        }
        initialPress = true;
    }
}

void PhotonMapping::cleanup() {
    VulkanBaseApp::cleanup();
}

void PhotonMapping::onPause() {
    VulkanBaseApp::onPause();
}

void PhotonMapping::endFrame() {
    if(inspect && selectMode != SelectMode::Off) {
        debug.enabled = true;

        debugInfo.cpu->cameraPosition = camera->position();
        debugInfo.cpu->target = mousePositionToWorldSpace(camera->cam());
        debugInfo.cpu->mode = static_cast<int>(selectMode);

        device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
            photonMapInfo.mask = rayTraceInfo.cpu->mask;

            std::vector<VkDescriptorSet> sets{
                    raytrace.descriptorSet,
                    raytrace.instanceDescriptorSet,
                    raytrace.vertexDescriptorSet,
                    lightDescriptorSet,
                    debugDescriptorSet,
                    gBuffer.descriptorSet
            };
            assert(raytrace.pipeline);

            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT};
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytrace.pipeline.handle);
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytrace.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);

            vkCmdPushConstants(commandBuffer, raytrace.layout.handle, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(PhotonMapInfo), &photonMapInfo);

            vkCmdTraceRaysKHR(commandBuffer, bindingTables.rayGens["debug_rgen"], bindingTables.miss, bindingTables.closestHit,
                              bindingTables.callable, 1, 1, 1);

            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_HOST_BIT
                    , 0, 1, &barrier, 0, nullptr, 0, nullptr);

        });

        selectMode = static_cast<SelectMode>(debugInfo.cpu->mode);
    }

    if(!ImGui::IsAnyItemActive() && computeNearestNeighbour && debugInfo.cpu->radius > 0) {
        computeNearestNeighbour = false;
        debugInfo.cpu->numNeighboursFound = 0;
        debugInfo.cpu->numNeighbours = numNeighbours;
//        device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
//            std::array<VkDescriptorSet, 2> sets{};
//            sets[0] = lightDescriptorSet;
//            sets[1] = debugDescriptorSet;
//
//            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT};
//            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &barrier, 0, nullptr, 0, nullptr);
//
//
//            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, searchCompute.pipeline.handle);
//            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, searchCompute.layout.handle
//                                    , 0, sets.size(), sets.data(), 0, nullptr);
//            vkCmdDispatch(commandBuffer, 1, 1, 1);
//
//            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
//            barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
//            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_HOST_BIT
//                    , 0, 1, &barrier, 0, nullptr, 0, nullptr);
//        });

        std::span<int> tree = {reinterpret_cast<int*>(kdTree.gpu.map()), static_cast<size_t>(stats.treeSize)};
        std::span<Photon> photons = { reinterpret_cast<Photon*>(generatedPhotons.map()), static_cast<size_t>(stats.treeSize) };
        auto neighbours = kdtree::search(tree, photons, debugInfo.cpu->hitPosition, debugInfo.cpu->radius); //, debugInfo.cpu->numNeighbours);

        debugInfo.cpu->numNeighboursFound = neighbours.size();
        debugInfo.cpu->numNeighbours = std::max(debugInfo.cpu->numNeighboursFound, numNeighbours);

        nearestNeighbourBuffer.copy(neighbours);

        generatedPhotons.unmap();
        kdTree.gpu.unmap();

//        assert(debugInfo.cpu->numNeighboursFound <= numNeighbours);

    }

    if(frameCount % 10 == 0) {
//        computeIndirectRadiance();
    }

    if(cameraActive()) {
        device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
            VkClearColorValue clearColor{ {0.f, 0.f, 0.f, 0.f}};
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdClearColorImage(commandBuffer, gBuffer.indirectLight.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &range);
        });
    }
}

bool PhotonMapping::cameraActive() {
    using namespace std::chrono_literals;
    auto duration = chrono::steady_clock::now() - lastCameraUpdate;
    return duration < 100ms;
}

void PhotonMapping::computeIndirectRadiance() {
    if(!computeIR.run) return;

    uint32_t blockSize = computeIR.constants.blockSize;
    computeIR.numIterations = (width * height)/(blockSize * blockSize);
    computeIR.constants.width = width;
    computeIR.constants.height = height;
    computeIR.constants.radius = photonMapInfo.searchDistance;
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        static std::array<VkDescriptorSet, 2> sets;
        sets[0] = gBuffer.descriptorSet;
        sets[1] = lightDescriptorSet;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computeIR.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computeIR.layout.handle, 0, sets.size(), sets.data(), 0, nullptr);
        vkCmdPushConstants(commandBuffer, computeIR.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(computeIR.constants), &computeIR.constants);
        vkCmdDispatch(commandBuffer, blockSize, blockSize, 1);
    });

    computeIR.constants.iteration++;

    progress = float(computeIR.constants.iteration)/float(computeIR.numIterations) * 100;

    if(computeIR.constants.iteration > computeIR.numIterations) {
        computeIR.run = false;
        computeIR.constants.iteration = 0;
    }
}

void PhotonMapping::computeIndirectRadianceCPU() {
    computeIR.run = true;
    VkDeviceSize offset = 0;
    VkDeviceSize imageSize = width * height * sizeof(float) * 4;
    uvec2 screen{ width, height};
    
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        VkImageSubresourceLayers subResource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        VkBufferImageCopy region{0, 0, 0, subResource, {0, 0, 0}, { screen.x, screen.y, 1}};
        vkCmdCopyImageToBuffer(commandBuffer, gBuffer.color.image, VK_IMAGE_LAYOUT_GENERAL, scratchBuffer, 1, &region);

        offset += imageSize;
        region.bufferOffset = offset;
        vkCmdCopyImageToBuffer(commandBuffer, gBuffer.position.image, VK_IMAGE_LAYOUT_GENERAL, scratchBuffer, 1, &region);

        offset += imageSize;
        region.bufferOffset = offset;
        vkCmdCopyImageToBuffer(commandBuffer, gBuffer.normal.image, VK_IMAGE_LAYOUT_GENERAL, scratchBuffer, 1, &region);

        offset += imageSize;
//        region.bufferOffset = offset;
//        vkCmdCopyImageToBuffer(commandBuffer, gBuffer.indirectLight.image, VK_IMAGE_LAYOUT_GENERAL, scratchBuffer, 1, &region);
        vkCmdFillBuffer(commandBuffer, scratchBuffer, offset, imageSize, 0);

        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT
                                , 0, 1, &barrier, 0, nullptr, 0, nullptr);
    });

    runInBackground([&]{
        size_t screenSize = width * height;
        auto memory = reinterpret_cast<glm::vec4*>(scratchBuffer.map());
        auto begin = memory;
        auto end = memory + screenSize;
        std::vector<glm::vec4> colorBuffer{ begin, end};

        begin += screenSize;
        end += screenSize;

        std::vector<glm::vec4> positionBuffer{ begin, end };

        begin += screenSize;
        end += screenSize;

        std::vector<glm::vec4> normalBuffer { begin, end };

        begin += screenSize;
        end += screenSize;

        std::vector<glm::vec4> indirectRadianceBuffer(screenSize);
        auto radius = photonMapInfo.searchDistance;

        std::vector<int> tree(stats.treeSize);
        std::vector<Photon> photons(stats.photonCount);
        std::memcpy(tree.data(), kdTree.gpu.map(), stats.treeSize * sizeof(int));
        std::memcpy(photons.data(), reinterpret_cast<Photon*>(generatedPhotons.map()), stats.photonCount * sizeof(Photon));

        scratchBuffer.unmap();
        generatedPhotons.unmap();
        kdTree.gpu.unmap();

        auto start = std::chrono::high_resolution_clock::now();
        float count = 0;
        for(int y = 0; y < height; y++){
            for(int x = 0; x < width; x++){

                count++;
                progress = (count * 100)/(width * height);

                glm::ivec2 idx{x, y};
                auto index = y * width + x;
                auto color = colorBuffer[index];

                if(color.a == 0) continue;

                auto position = positionBuffer[index].xyz();
                auto result = kdtree::search(tree, photons, position, radius, photonMapInfo.neighbourCount);

                if(!result.empty()) {
                    auto normal = normalBuffer[index].xyz();
                    auto N = glm::normalize(normal);

                    glm::vec3 radiance{0};
                    for(auto& photon : result) {
                        auto L = glm::normalize(-photon->direction);
                        radiance += color.rgb * glm::max(0.f, glm::dot(N, L)) * photon->power.rgb;
                    }
                    radiance /= glm::pi<float>() * radius * radius;

                    indirectRadianceBuffer[index] = glm::vec4{ radiance, 1};
                }
            }
        }
        auto duration = std::chrono::high_resolution_clock::now() - start;
        spdlog::info("indirect radiance calculated in {} seconds", std::chrono::duration_cast<std::chrono::seconds>(duration).count());

        VkDeviceSize imageSize = width * height * sizeof(float) * 4;
        VkDeviceSize bufferOffset = imageSize * 3;
        scratchBuffer.copy(indirectRadianceBuffer.data(), imageSize, bufferOffset);

        onIdle([&]{
            device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
                VkDeviceSize bufferOffset = width * height * sizeof(float) * 4 * 3;
                VkImageSubresourceLayers subResource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                VkBufferImageCopy region{bufferOffset, 0, 0, subResource, {0, 0, 0}, { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1}};

                VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT};
                vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT
                        , 0, 1, &barrier, 0, nullptr, 0, nullptr);
                vkCmdCopyBufferToImage(commandBuffer, scratchBuffer, gBuffer.indirectLight.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
            });
           computeIR.run = false;
        });
    });
}

void PhotonMapping::computeIndirectRadianceCPU2(){
    computeIR.run = true;
    VkDeviceSize offset = 0;
    VkDeviceSize imageSize = width * height * sizeof(float) * 4;
    uvec2 screen{ width, height};

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        VkImageSubresourceLayers subResource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        VkBufferImageCopy region{0, 0, 0, subResource, {0, 0, 0}, { screen.x, screen.y, 1}};
        vkCmdCopyImageToBuffer(commandBuffer, gBuffer.color.image, VK_IMAGE_LAYOUT_GENERAL, scratchBuffer, 1, &region);

        offset += imageSize;
        region.bufferOffset = offset;
        vkCmdCopyImageToBuffer(commandBuffer, gBuffer.position.image, VK_IMAGE_LAYOUT_GENERAL, scratchBuffer, 1, &region);

        offset += imageSize;
        region.bufferOffset = offset;
        vkCmdCopyImageToBuffer(commandBuffer, gBuffer.normal.image, VK_IMAGE_LAYOUT_GENERAL, scratchBuffer, 1, &region);

        offset += imageSize;
        vkCmdFillBuffer(commandBuffer, scratchBuffer, offset, imageSize, 0);

        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT
                , 0, 1, &barrier, 0, nullptr, 0, nullptr);
    });

    auto blockSize = computeIR.constants.blockSize;
    auto screenSize = width * height;
    auto numTasks = screenSize/(blockSize * blockSize);
    auto memory = reinterpret_cast<glm::vec4*>(scratchBuffer.map());
    auto begin = memory;
    auto end = memory + screenSize;
    std::vector<glm::vec4> colorBuffer{ begin, end};

    begin += screenSize;
    end += screenSize;

    std::vector<glm::vec4> positionBuffer{ begin, end };

    begin += screenSize;
    end += screenSize;

    std::vector<glm::vec4> normalBuffer { begin, end };


    std::vector<int> tree(stats.treeSize);
    std::vector<Photon> photons(stats.photonCount);
    std::memcpy(tree.data(), kdTree.gpu.map(), stats.treeSize * sizeof(int));
    std::memcpy(photons.data(), reinterpret_cast<Photon*>(generatedPhotons.map()), stats.photonCount * sizeof(Photon));

    scratchBuffer.unmap();
    generatedPhotons.unmap();
    kdTree.gpu.unmap();
    static std::atomic_int taskFinishedCount;
    static std::atomic<float> pixelsProcessed ;
    taskFinishedCount = 0;
    pixelsProcessed = 0;
    totalDuration = 0;
    progress = 0;

    for(int taskId = 0; taskId < numTasks; ++taskId){
        runInBackground([&, taskId, numTasks, tree, photons, blockSize, colorBuffer, positionBuffer, normalBuffer
                         , screen, numPhotons = photonMapInfo.neighbourCount, radius = photonMapInfo.searchDistance]() mutable {
            auto width = screen.x;
            auto height = screen.y;

            auto row = (taskId * blockSize)/width;
            auto col = taskId % (width/blockSize);

            ivec2 offset{col * blockSize, row * blockSize };
            uvec2 extent{static_cast<unsigned int>(blockSize)};

            spdlog::info("task{}: offset: [{}, {}], extent: [{}, {}]"
                         , taskId, offset.x, offset.y, extent.x, extent.y);

            auto start = std::chrono::high_resolution_clock::now();
            std::vector<glm::vec4> indirectRadianceBuffer(blockSize * blockSize);
            float count = 0;
            for(int i = 0; i < blockSize; ++i){
                for(int j = 0; j < blockSize; ++j){

//                    atomic_fetch_add(&pixelsProcessed, 1);
//                    progress = (pixelsProcessed.load() * 100)/(width * height);
//                    count++;
//                    progress = (count * 100)/(width * height);

                    auto x = j + offset.x;
                    auto y = i + offset.y;
                    glm::ivec2 idx{x, y};
                    auto index = y * width + x;
                    auto color = colorBuffer[index];

                    if(color.a == 0) continue;

                    auto position = positionBuffer[index].xyz();
                    auto result = kdtree::search(tree, photons, position, radius, numPhotons);

                    if(!result.empty()) {
                        auto normal = normalBuffer[index].xyz();
                        auto N = glm::normalize(normal);

                        glm::vec3 radiance{0};
                        for(auto& photon : result) {
                            auto L = glm::normalize(-photon->direction);
                            radiance += color.rgb * glm::max(0.f, glm::dot(N, L)) * photon->power.rgb;
                        }
                        radiance /= glm::pi<float>() * radius * radius;

                        index = i * blockSize + j;
                        indirectRadianceBuffer[index] = glm::vec4{ radiance, 1};
                    }
                }
            }
            auto duration = std::chrono::high_resolution_clock::now() - start;
            spdlog::info("task{}: indirect radiance calculated in {} seconds"
                         , taskId, std::chrono::duration_cast<std::chrono::seconds>(duration).count());
            atomic_fetch_add(&totalDuration, int(std::chrono::duration_cast<std::chrono::seconds>(duration).count()));
            taskFinishedCount++;

            VkDeviceSize imageSize = indirectRadianceBuffer.size() * sizeof(glm::vec4);
            onIdle([&, taskId, offset, extent, width, height, indirectRadianceBuffer, numTasks, stagingBuffer = device.createStagingBuffer(imageSize)]{
                device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){

                    stagingBuffer.copy(indirectRadianceBuffer);

                    VkImageSubresourceLayers subResource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                    VkBufferImageCopy region{0, 0, 0, subResource, {offset.x, offset.y, 0}, {extent.x, extent.y, 1}};

                    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT};
                    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT
                            , 0, 1, &barrier, 0, nullptr, 0, nullptr);
                    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, gBuffer.indirectLight.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
                });
                progress += (computeIR.constants.blockSize * computeIR.constants.blockSize * 100)/(width * height);
                if(taskFinishedCount == numTasks){
                    computeIR.run = false;
                    spdlog::info("total IR runtime: {} seconds", totalDuration.load()/numTasks);
                }
            });
        });
    }
}

void PhotonMapping::newFrame() {
    camera->newFrame();
}


int main(){
    try{

        Settings settings;
        settings.width = 1024;
        settings.height = 1024;
        settings.depthTest = true;
        settings.enableBindlessDescriptors = false;
        settings.enabledFeatures.geometryShader = VK_TRUE;
        settings.enabledFeatures.wideLines = VK_TRUE;
        settings.deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = PhotonMapping{ settings };
        app.addPlugin(imGui);

        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}