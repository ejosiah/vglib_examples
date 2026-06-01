#include "PlanetDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

#include <glm/glm.hpp>
#include <ccmesh.h>

#include <fstream>
#include <iomanip>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <string_view>

#include "math/geometry.hpp"

namespace {
    template <typename T>
    void write_field(std::ofstream& file, std::string_view name, const T& value) {
        file << name << " = " << value << '\n';
    }

    void write_field(std::ofstream& file, std::string_view name, const glm::vec2& value) {
        file << name << " = { " << value.x << ", " << value.y << " }\n";
    }

    void write_field(std::ofstream& file, std::string_view name, const glm::vec3& value) {
        file << name << " = { " << value.x << ", " << value.y << ", " << value.z << " }\n";
    }

    void write_field(std::ofstream& file, std::string_view name, const glm::vec4& value) {
        file << name << " = { " << value.x << ", " << value.y << ", " << value.z << ", " << value.w << " }\n";
    }

    void write_matrix(std::ofstream& file, std::string_view name, const glm::mat4& value) {
        for (uint32_t row = 0; row < 4; ++row)
            for (uint32_t col = 0; col < 4; ++col)
                file << name << '[' << row << " * " << col << "] = " << value[col][row] << '\n';
    }

    std::ofstream open_constant_dump(const fs::path& frameDir, const char* fileName) {
        std::ofstream file(frameDir / fileName);
        file << std::setprecision(17);
        return file;
    }

    void write_constant_buffer_dumps(const std::string& projectDir, uint32_t frameIndex, const GlobalCB& globalCB,
                                     const Planet& planet) {
        const auto frameDir = fs::path(projectDir) / "buffer_logs" / fmt::format("frame_{:06}", frameIndex);
        fs::create_directories(frameDir);

        {
            auto file = open_constant_dump(frameDir, "constants.globalCB.txt");
            write_matrix(file, "ViewProjectionMatrix", globalCB.ViewProjectionMatrix);
            write_matrix(file, "InvViewProjectionMatrix", globalCB.InvViewProjectionMatrix);
            write_field(file, "CameraPosition", globalCB.CameraPosition);
            write_field(file, "SunDirection", globalCB.SunDirection);
            write_field(file, "WireFrameColor", globalCB.WireFrameColor);
            write_field(file, "ScreenSize", globalCB.ScreenSize);
            write_field(file, "FrameIndex", globalCB.FrameIndex);
            write_field(file, "Time", globalCB.Time);
            write_field(file, "CullFlag", globalCB.CullFlag);
            write_field(file, "FoV", globalCB.FoV);
            write_field(file, "WireFrameSize", globalCB.WireFrameSize);
            write_field(file, "ScreenSpaceShadow", globalCB.ScreenSpaceShadow);
            write_field(file, "FarPlaneDistance", globalCB.FarPlaneDistance);
        }

        {
            const auto& updateCB = planet.get_update_cb_data();
            auto file = open_constant_dump(frameDir, "constants.earth.updateCB.txt");
            write_matrix(file, "ViewProjectionMatrix", updateCB.ViewProjectionMatrix);
            write_matrix(file, "InvViewProjectionMatrix", updateCB.InvViewProjectionMatrix);
            for (uint32_t idx = 0; idx < updateCB.FrustumPlanes.size(); ++idx)
                write_field(file, fmt::format("FrustumPlanes[{}]", idx), updateCB.FrustumPlanes[idx]);
            write_field(file, "CameraPosition", updateCB.CameraPosition);
            write_field(file, "CameraForward", updateCB.CameraForward);
            write_field(file, "TriangleSize", updateCB.TriangleSize);
            write_field(file, "MaxSubdivisionDepth", updateCB.MaxSubdivisionDepth);
            write_field(file, "FOV", updateCB.FOV);
            write_field(file, "FarPlaneDistance", updateCB.FarPlaneDistance);
        }

        {
            const auto& geometryCB = planet.get_geometry_cb_data();
            auto file = open_constant_dump(frameDir, "constants.earth.geometryCB.txt");
            write_field(file, "TotalNumElements", geometryCB.TotalNumElements);
            write_field(file, "BaseDepth", geometryCB.BaseDepth);
            write_field(file, "TotalNumVertices", geometryCB.TotalNumVertices);
            write_field(file, "MaterialID", geometryCB.MaterialID);
        }

        {
            const auto& planetCB = planet.get_planet_cb_data();
            auto file = open_constant_dump(frameDir, "constants.earth.planetCB.txt");
            write_field(file, "PlanetCenter", planetCB.center);
            write_field(file, "PlanetRadius", planetCB.radius);
        }
    }

struct EdgeKey {
    int32_t a;
    int32_t b;

    bool operator==(const EdgeKey& other) const {
        return a == other.a && b == other.b;
    }
};

struct EdgeKeyHash {
    size_t operator()(const EdgeKey& e) const {
        return (size_t(e.a) << 32u) ^ size_t(e.b);
    }
};

static cc_Mesh* ConvertIndexedMeshToCcMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, bool isQuadMesh = false) {
    const int32_t faceSize = isQuadMesh ? 4 : 3;

    if (vertices.empty() || indices.empty() || indices.size() % faceSize != 0) return nullptr;

    std::unordered_map<EdgeKey, int32_t, EdgeKeyHash> undirectedEdges;

    for (size_t i = 0; i < indices.size(); i += faceSize) {
        for (int32_t j = 0; j < faceSize; ++j) {
            const int32_t a = int32_t(indices[i + j]);
            const int32_t b = int32_t(indices[i + ((j + 1) % faceSize)]);

            if (a < 0 || a >= int32_t(vertices.size()) || b < 0 || b >= int32_t(vertices.size())) return nullptr;

            const EdgeKey key{ std::min(a, b), std::max(a, b) };
            if (undirectedEdges.find(key) == undirectedEdges.end()) undirectedEdges[key] = int32_t(undirectedEdges.size());
        }
    }

    const int32_t vertexCount = int32_t(vertices.size());
    const int32_t uvCount = int32_t(vertices.size());
    const int32_t halfedgeCount = int32_t(indices.size());
    const int32_t edgeCount = int32_t(undirectedEdges.size());
    const int32_t faceCount = int32_t(indices.size() / faceSize);

    cc_Mesh* mesh = ccm_Create(vertexCount, uvCount, halfedgeCount, edgeCount, faceCount);
    if (!mesh) return nullptr;

    std::fill(mesh->vertexToHalfedgeIDs, mesh->vertexToHalfedgeIDs + vertexCount, -1);
    std::fill(mesh->edgeToHalfedgeIDs, mesh->edgeToHalfedgeIDs + edgeCount, -1);
    std::fill(mesh->faceToHalfedgeIDs, mesh->faceToHalfedgeIDs + faceCount, -1);

    for (int32_t i = 0; i < vertexCount; ++i) {
        mesh->vertexPoints[i] = cc_VertexPoint{ vertices[i].position.x, vertices[i].position.y, vertices[i].position.z };
        mesh->uvs[i] = cc_VertexUv{ vertices[i].uv.x, vertices[i].uv.y };
    }

    for (int32_t i = 0; i < edgeCount; ++i) {
        mesh->creases[i] = cc_Crease{ -1, -1, 0.0f };
    }

    std::unordered_map<EdgeKey, int32_t, EdgeKeyHash> directedHalfedges;

    for (int32_t faceID = 0; faceID < faceCount; ++faceID) {
        const int32_t baseH = faceID * faceSize;
        mesh->faceToHalfedgeIDs[faceID] = baseH;

        for (int32_t j = 0; j < faceSize; ++j) {
            const int32_t h = baseH + j;
            const int32_t nextH = baseH + ((j + 1) % faceSize);
            const int32_t prevH = baseH + ((j + faceSize - 1) % faceSize);
            const int32_t v = int32_t(indices[h]);

            mesh->halfedges[h] = cc_Halfedge{ -1, nextH, prevH, faceID, -1, v, v };
            if (mesh->vertexToHalfedgeIDs[v] == -1) mesh->vertexToHalfedgeIDs[v] = h;
        }

        for (int32_t j = 0; j < faceSize; ++j) {
            const int32_t h = baseH + j;
            const int32_t from = int32_t(indices[h]);
            const int32_t to = int32_t(indices[baseH + ((j + 1) % faceSize)]);

            const EdgeKey undirectedKey{ std::min(from, to), std::max(from, to) };
            const EdgeKey directedKey{ from, to };
            const EdgeKey reverseKey{ to, from };

            const int32_t edgeID = undirectedEdges[undirectedKey];

            mesh->halfedges[h].edgeID = edgeID;
            if (mesh->edgeToHalfedgeIDs[edgeID] == -1) mesh->edgeToHalfedgeIDs[edgeID] = h;

            auto twinIt = directedHalfedges.find(reverseKey);
            if (twinIt != directedHalfedges.end()) {
                const int32_t twinID = twinIt->second;
                mesh->halfedges[h].twinID = twinID;
                mesh->halfedges[twinID].twinID = h;
            }

            directedHalfedges[directedKey] = h;
        }
    }

    return mesh;
}

 bool triangleLineTest(vec3 a, vec3 b, vec3 c, vec3 p, vec3 q, float& t, vec3& uvw) {
    vec3 ab = b - a;
    vec3 ac = c - a;
    vec3 qp = p - q;
    vec3 n = cross(ab, ac);

    float d = dot(qp, n);
    if (abs(d) < 1e-8f) return false;

    vec3 ap = p - a;
    float ood = 1.0f / d;

    t = dot(ap, n) * ood;
    if (t < 0.0f || t > 1.0f) return false;

    vec3 e = cross(qp, ap);

    float v = dot(ac, e) * ood;
    if (v < 0.0f || v > 1.0f) return false;

    float w = -dot(ab, e) * ood;
    if (w < 0.0f || v + w > 1.0f) return false;

    uvw = vec3(1.0f - v - w, v, w);
    return true;
}

    using Triangle = std::tuple<glm::vec3, glm::vec3, glm::vec3>;

std::optional<Triangle> findClosestHit(std::span<glm::vec3> vertices, glm::vec3 origin, glm::vec3 target) {

    std::optional<Triangle> closestTri{};
    float dist = MAX_FLOAT;
    const auto numVertices = vertices.size();
    for (auto i = 0; i < numVertices; i+= 3) {
        auto v0 = vertices[i];
        auto v1 = vertices[i+1];
        auto v2 = vertices[i+2];

        glm::vec3 bc; float t;
        auto hit = triangleLineTest(v0, v1, v2, origin, target, t, bc);
        if (hit) {
            auto center = v0 * bc.x + v1 * bc.y + v2 * bc.z;
            auto hitDist = glm::distance(origin, center);
            if (hitDist < dist) {
                closestTri = std::make_tuple(v0, v1, v2);
                dist = hitDist;
            }
        }
    }

    return closestTri;
}

// std::optional<Triangle> findClosestHit(std::span<glm::vec3> vertices, std::span<glm::uint32> indexes, glm::vec3 origin, glm::vec3 target) {
//
//     std::optional<Triangle> closestTri{};
//     float dist = MAX_FLOAT;
//     const auto numIndexes = indexes.size();
//     for (auto i = 0; i < numIndexes; i+= 3) {
//         auto v0 = vertices[indexes[i]];
//         auto v1 = vertices[indexes[i+1]];
//         auto v2 = vertices[indexes[i+2]];
//
//         glm::vec3 bc; float t;
//         auto hit = triangleLineTest(v0, v1, v2, origin, target, t, bc);
//         if (hit) {
//             auto center = v0 * bc.x + v1 * bc.y + v2 * bc.z;
//             auto hitDist = glm::distance(origin, center);
//             if (hitDist < dist) {
//                 closestTri = std::make_tuple(v0, v1, v2);
//                 dist = hitDist;
//             }
//         }
//     }
//
//     return closestTri;
// }

std::optional<Triangle> findClosestHit(std::span<glm::vec3> vertices, std::span<uint32_t> indexbc, glm::vec3 origin, glm::vec3 target, uint32_t numTris) {

    std::optional<Triangle> closestTri{};
    float dist = MAX_FLOAT;
    for (auto i = 0u; i < numTris; i++) {
        auto tri = indexbc[i];
        auto v0 = vertices[tri * 3 + 2];
        auto v1 = vertices[tri * 3 + 1];
        auto v2 = vertices[tri * 3 + 0];

        glm::vec3 bc; float t;
        auto hit = triangleLineTest(v0, v1, v2, origin, target, t, bc);
        if (hit) {
            auto center = v0 * bc.x + v1 * bc.y + v2 * bc.z;
            auto hitDist = glm::distance(origin, center);
            if (hitDist < dist) {
                closestTri = std::make_tuple(v0, v1, v2);
                dist = hitDist;
            }
        }
    }

    return closestTri;
}

}

PlanetDemo::PlanetDemo(const Settings& settings)
    : VulkanBaseApp("Planet", settings)
     {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("planet");
    fileManager().addSearchPathFront("planet/data");
    fileManager().addSearchPathFront("planet/spv");
    fileManager().addSearchPathFront("planet/models");
    fileManager().addSearchPathFront("planet/textures");
}

void PlanetDemo::initApp() {
    initCamera();
    createDescriptorPool();
    initBindlessDescriptor();
    loadTextures();
    creatSkyBox();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    createBuffers();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    initGeometry();
    initLoader();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    prepareRender();
}

void PlanetDemo::createBuffers() {
    globalBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(GlobalCB), "global_buffer");
    global = static_cast<GlobalCB *>(globalBuffer.map());
    *global = GlobalCB{};
}

void PlanetDemo::prepareRender() {
    device.graphicsCommandPool().oneTimeCommand([this](auto cmd) {
        m_FrameIndex = UINT32_MAX;
        m_Time = 0;

        updateConstantBuffers();
        m_updateCB.ViewProjectionMatrix = glm::scale(glm::mat4{1}, glm::vec3(0));
        m_updateCB.InvViewProjectionMatrix = glm::scale(glm::mat4{1}, glm::vec3(0));
        m_EarthPlanet.update_constant_buffers(m_updateCB);
        m_MeshUpdater.reset_buffers(cmd, m_EarthPlanet.m_descriptorSet, m_EarthPlanet.m_CBTDescriptorSet);
        m_MeshUpdater.prepare_indirection(cmd, m_EarthPlanet);
        m_EarthPlanet.evaluate_leb(cmd, globalDescriptorSet, true, true);
        m_WaterDeformer.apply_deformation(cmd, m_EarthPlanet);
    });
    m_FrameIndex = 0;
}

void PlanetDemo::initGeometry() {
    // Num elements the CBT holds
    const auto cbtNumElements = cbt_large::cbt_num_elements(m_CBTType);
    cbt_large::CBT* cbt = create_cbt(m_CBTType);
    planetMesh = CPUMesh::load_cpu_mesh(resource("icosahedron.ccm"), cbtNumElements);
    m_LebMatrixCache.intialize(device, LEB_MATRIX_CACHE_SIZE);

    m_EarthPlanet = Planet{ { "earth", device, globalDescriptorSetLayout, g_EarthRadius, g_EarthCenter, g_EarthImpostorToggle, g_EarthTriangleSize, EARTH_MATERIAL } };
    m_EarthPlanet.initialize(*cbt, planetMesh);
    m_EarthPlanet.updateLEBDescriptorSet(m_LebMatrixCache.get_leb_matrix_buffer());

    m_EarthRenderer = { { device, m_EarthPlanet, globalDescriptorSetLayout} };
    m_EarthRenderer.initialize();

    m_MoonPlanet = Planet{ { "moon", device, globalDescriptorSetLayout, g_MoonRadius, g_MoonCenter, g_MoonImpostorToggle, g_MoonTriangleSize, MOON_MATERIAL } };
    m_MoonPlanet.initialize(*cbt, planetMesh);
    m_MoonPlanet.updateLEBDescriptorSet(m_LebMatrixCache.get_leb_matrix_buffer());

    // TODO - move into planet and only create one CBTType shader, reload shader based on CBTType
    m_MeshUpdater = {device, globalDescriptorSetLayout};
    m_MeshUpdater.initialize(globalDescriptorSet);


    m_WaterDeformer = { device };
    m_WaterDeformer.initialize();



    std::vector<mesh::Mesh> meshes;
    mesh::load(meshes, resource("positions_mesh.ply"));
    const auto& mesh = meshes.front();
    // auto lproxy = primitives::sphere(4, 4, 1.f, glm::mat4{1}, {1, 0, 0, 1}, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    auto lproxy = primitives::plane(1, 1, 2, 2, glm::mat4{1}, glm::vec4{1}, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    auto vertices = map_range(lproxy.vertices, [](const auto& v){ return v.position.xyz(); });
    proxy.vertices = device.createCpuVisibleBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    proxy.indexes = device.createCpuVisibleBuffer(lproxy.indices.data(), BYTE_SIZE(lproxy.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    // lproxy.indices = {2, 0, 1, 3};
    // auto ccMesh = ConvertIndexedMeshToCcMesh(lproxy.vertices, lproxy.indices, true);
    // auto path = (fs::current_path() / "../data/models/quad.ccm");
    // fs::remove(path);
    // ccm_Save(ccMesh, path.string().c_str());

    delete cbt;
}

void PlanetDemo::initCamera() {
    // OrbitingCameraSettings cameraSettings;
    // cameraSettings.orbitMinZoom = 0.1;
    // cameraSettings.orbitMaxZoom = 512.0f;
    // cameraSettings.offsetDistance = 1.0f;
    // cameraSettings.modelHeight = 1.5;
    // cameraSettings.fieldOfView = 60.0f;
    // cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);
    //
    // camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);

    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.fieldOfView = g_CameraFOV;
    cameraSettings.zNear = 0.1;
    // cameraSettings.zFar =  g_EarthRadius + 100000; //200000;
    cameraSettings.zFar =  200;
    // cameraSettings.acceleration = glm::vec3(500 * km);
    //    // cameraSettings.velocity = glm::vec3(1000 * km);
    cameraSettings.aspectRatio = static_cast<float>(width) / static_cast<float>(height);

    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    // float yaw = -0.1f;
    // float pitch = -glm::pi<float>() / 4.2f;
    // auto rotX = glm::rotate(glm::mat4{1}, pitch, {1, 0, 0});
    // auto rotY = glm::rotate(glm::mat4{1}, yaw, {0, 1, 0});
    //
    // glm::vec3 pos{0, 0, -(g_EarthRadius + 10000)};
    // // pos = (rotX  * rotY * glm::vec4(pos, 1)).xyz();
    // auto target = pos + glm::vec3{0.0011957388, 0.9735858440, 0.2283589840};
    camera->lookAt({0, 0, 5}, glm::vec3(0, 0, 0), {0, 1, 0});
    // camera->position(pos);
    // camera->rotate(glm::degrees(yaw), glm::degrees(pitch), 0);
}

void PlanetDemo::loadTextures() {
    textures::fromFile(device, milkyway, resource("milky_way/milky_way.png"), false, VK_FORMAT_R8G8B8A8_SRGB);
}

void PlanetDemo::newFrame() {
    camera->newFrame();
    updateConstantBuffers();
}

void PlanetDemo::creatSkyBox() {
    const auto cube = primitives::cube();
    const auto vertices = map_range(cube.vertices, [](const auto& v){ return v.position.xyz(); });
    const auto indexes = cube.indices;

    skybox.vertices = device.createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    skybox.indexes = device.createDeviceLocalBuffer(indexes.data(), BYTE_SIZE(indexes), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void PlanetDemo::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void PlanetDemo::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    auto devFeatures12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    devFeatures12->scalarBlockLayout = VK_TRUE;
    devFeatures12->shaderBufferInt64Atomics = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);


}

void PlanetDemo::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 3> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}


void PlanetDemo::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void PlanetDemo::createDescriptorSetLayouts() {
    textureDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("texture_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();

    globalDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("global_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
}

void PlanetDemo::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( { textureDescriptorSetLayout, globalDescriptorSetLayout });
    milkywayDescriptorSet = sets[0];
    globalDescriptorSet = sets[1];

    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("milkyway_descriptor_set", milkywayDescriptorSet);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("global_descriptor_set", globalDescriptorSet);

    auto writes = initializers::writeDescriptorSets<2>();
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = milkywayDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo dispInfo{ milkyway.sampler.handle, milkyway.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[0].pImageInfo = &dispInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = globalDescriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo globalInfo{ globalBuffer, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &globalInfo;

    device.updateDescriptorSets(writes);
}

void PlanetDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void PlanetDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void PlanetDemo::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.primitive.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("proxy.vert.spv"))
                    .fragmentShader(resource("proxy.frag.spv"))
                .vertexInputState().clear()
                    .addVertexBindingDescription(0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX)
                    .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
                .rasterizationState()
                    .cullNone()
                    // .polygonModeLine()
                .name("proxy_render")
                .build(render.primitive.layout);

        render.skybox.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("skybox/skybox.vert.spv"))
                    .fragmentShader(resource("equi_rect.frag.spv"))
                .vertexInputState().clear()
                    .addVertexBindingDescription(0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX)
                    .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
                .rasterizationState()
                    .cullFrontFace()
                .depthStencilState()
                    .compareOpLessOrEqual()
                .layout()
                    .addDescriptorSetLayout(textureDescriptorSetLayout)
                .name("skybox_render")
                .build(render.skybox.layout);
    //    @formatter:on
}


void PlanetDemo::onSwapChainDispose() {
    dispose(render.primitive.pipeline);
}

void PlanetDemo::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *PlanetDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    if (advanceFrame) {
        m_MeshUpdater.update(commandBuffer, globalDescriptorSet, m_EarthPlanet);
        m_EarthPlanet.evaluate_leb(commandBuffer, globalDescriptorSet, m_RayTracingPath);
        m_WaterDeformer.apply_deformation(commandBuffer, m_EarthPlanet);
        if (m_EnableFileLogging && m_FileLoggingFrameCount < m_MaxFileLoggingFrames) {
            m_MeshUpdater.capture_frame_buffer_dumps(commandBuffer, m_EarthPlanet.m_CBTMesh, m_EarthPlanet.m_BaseMesh,
                                                     m_LebMatrixCache.get_leb_matrix_buffer(), m_FrameIndex);
            m_PendingBufferDumpFrameIndex = m_FrameIndex;
            ++m_FileLoggingFrameCount;
        }
        advanceFrame = false;
    }

    clearColor(0, 0, 1);

    renderToSwapChain([&]{
        m_EarthRenderer.render(commandBuffer, *camera.get(), globalDescriptorSet);
        m_EarthPlanet.renderDebug(commandBuffer, globalDescriptorSet);

        VkDeviceSize offset = 0;
        static auto model = glm::scale(glm::mat4{1}, glm::vec3(g_EarthRadius));
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.primitive.pipeline.handle);
        camera->push(commandBuffer, render.primitive.layout);
        //
        // vkCmdBindVertexBuffers(commandBuffer, 0, 1, &proxy.vertices.buffer, &offset);
        // vkCmdBindIndexBuffer(commandBuffer, proxy.indexes,  0, VK_INDEX_TYPE_UINT32);
        // vkCmdDrawIndexed(commandBuffer, proxy.indexes.sizeAs<uint32_t>(), 1, 0, 0, 0);

        // vkCmdBindVertexBuffers(commandBuffer, 0, 1, m_EarthPlanet.m_BaseMesh.vertexBuffer, &offset);
        // vkCmdBindIndexBuffer(commandBuffer, m_EarthPlanet.m_BaseMesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        // vkCmdDrawIndexed(commandBuffer, m_EarthPlanet.m_BaseMesh.indexBuffer.sizeAs<uint32_t>(), 1, 0, 0, 0);

        renderSkyBox(commandBuffer);

        renderUI(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void PlanetDemo::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("controls");
    ImGui::SetWindowSize({200, 200});
    static bool play = false;
    if (ImGui::Button("advance")) {
       advanceFrame = true;
    } else {
        advanceFrame = play;
    }
    ImGui::SameLine();
    ImGui::Checkbox("play", &play);
    ImGui::Checkbox("logging", &m_EnableFileLogging);
    ImGui::InputScalar("log frame limit", ImGuiDataType_U32, &m_MaxFileLoggingFrames);

    ImGui::End();
    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void PlanetDemo::renderSkyBox(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.skybox.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.skybox.layout.handle, 0, 1, &milkywayDescriptorSet, 0, nullptr);
    camera->push(commandBuffer, render.skybox.layout);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, skybox.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, skybox.indexes, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, skybox.indexes.sizeAs<uint32_t>(), 1, 0, 0, 0);
}

void PlanetDemo::update(float time) {
    camera->update(time);
    setTitle(fmt::format("{}, camera : {}, view: {}, frame - {}", title, camera->position(), camera->viewDir, m_FrameIndex));
}

void PlanetDemo::checkAppInputs() {
    camera->processInput();

    // if (!ImGui::IsAnyItemActive() && mouse.left.released) {
    //     const auto cam = camera->camera;
    //     auto uv = mouse.position / glm::vec2{width, height};
    //     spdlog::info("mouse clicked at sp: {}: ", uv);
    //     auto d = glm::vec2{-1.0f + 2.0f * uv.x, 1.0f - 2.0f * uv.y};
    //     auto h = glm::inverse(cam.proj * cam.view) * glm::vec4(d.x, d.y, 1.0f, 1.0f);
    //     auto target = glm::vec3(h) / h.w;
    //
    //     auto origin = camera->position();
    //     spdlog::info("origin: {}, target: {}", origin, target);
    //
    //     auto draws = m_EarthPlanet.m_CBTMesh.indirectDrawBuffer.span<VkDrawIndirectCommand>();
    //     auto vertices = m_EarthPlanet.m_CBTMesh.currentVertexBuffer.span<glm::vec3>();
    //     auto indexes = m_EarthPlanet.m_CBTMesh.indexedBisectorBuffer.span<uint32_t>();
    //     auto closestHit = findClosestHit(vertices, indexes, origin, target, draws.front().vertexCount);
    //     if (closestHit.has_value()) {
    //         auto [v0, v1, v2] = *closestHit;
    //         spdlog::info("closest hit [{}, {}, {}]", v0, v1, v2);
    //     }else {
    //         spdlog::info("No hit found");
    //     }
    // }
}

void PlanetDemo::endFrame() {
    const auto projectDir = (fs::current_path() / "planet").string();
    m_MeshUpdater.write_pending_frame_buffer_dumps(projectDir);
    if (m_PendingBufferDumpFrameIndex) {
        write_constant_buffer_dumps(projectDir, *m_PendingBufferDumpFrameIndex, *global, m_EarthPlanet);
        m_PendingBufferDumpFrameIndex.reset();
    }

    if (m_MirrorPOV) {
        const auto cam = camera->camera;
        auto view = cam.view;
        view[3] = glm::vec4{0, 0, 0, 1};
        auto projection = cam.proj;
        auto viewProjection = projection * view;
        auto modelViewProjection = viewProjection;
        auto invViewProjection = glm::inverse(viewProjection);

        static Frustum frustum;
        Frustum::extractFrustum(frustum, modelViewProjection);

        m_updateCB.ViewProjectionMatrix = viewProjection;
        m_updateCB.InvViewProjectionMatrix = invViewProjection;
        m_updateCB.CameraPosition = camera->position();
        m_updateCB.CameraForward = -glm::vec3(view[0][2], view[1][2], view[2][2]);
        m_updateCB.FarPlaneDistance = camera->far();
        m_updateCB.FOV = glm::radians(camera->fov * 0.5f);
        std::memcpy(m_updateCB.FrustumPlanes.data(), frustum.cp.data(), BYTE_SIZE(frustum.cp));

        m_EarthPlanet.update_constant_buffers(m_updateCB);
    }
    m_Time = elapsedTime;

    if (advanceFrame) {
        ++m_FrameIndex;
    }
}

void PlanetDemo::cleanup() {
    loader->stop();
    m_LebMatrixCache.release();
    AppContext::shutdown();
}

void PlanetDemo::onPause() {
    VulkanBaseApp::onPause();
}

void PlanetDemo::updateConstantBuffers() {
    const auto cam = camera->camera;
    auto view = cam.view;
    view[3] = glm::vec4{0, 0, 0, 1};
    auto viewProjection = cam.proj * view;
    auto invViewProjection = glm::inverse(viewProjection);
    global->ViewProjectionMatrix = viewProjection;
    global->InvViewProjectionMatrix = invViewProjection;
    global->CameraPosition = camera->position();
    global->FoV = glm::radians(camera->fov * 0.5f);
    global->ScreenSize = {width, height};
    global->SunDirection = glm::vec3{glm::inversesqrt(3.f)}; // TODO update
    global->FrameIndex = m_FrameIndex;
    global->Time = elapsedTime;
    global->FarPlaneDistance = camera->far();
    global->WireFrameColor = m_WireframeColor;
    global->WireFrameSize = m_WireframeSize;
    global->ScreenSpaceShadow = m_RayTracingPath ? 1.0f : 0.0f;
}


int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1920;
        settings.height = 1080;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enabledFeatures.geometryShader = true;
        settings.enabledFeatures.shaderFloat64 = true;
        settings.enabledFeatures.shaderInt64 = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_SHADER_ATOMIC_INT64_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = PlanetDemo{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}
