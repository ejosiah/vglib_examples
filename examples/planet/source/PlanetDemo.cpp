#include "PlanetDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

#include <glm/glm.hpp>
#include <ccmesh.h>

#include <cstdint>
#include <optional>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <array>

#include "math/geometry.hpp"

namespace {

    glm::mat4 view;
    glm::mat4 viewProjection;
    glm::mat4 invViewProjection;
    PlanetFrustum frustum;
    PlanetScalar earthDistance;
    bool earthIsVisible{};
    bool earthIsUpdatable{};
    PlanetScalar moonDistance;
    bool moonIsVisible{};
    bool moonIsUpdatable{};
    
glm::vec3 toAtmosphereVec3(const PlanetVec3& value) {
    return {
        static_cast<float>(value.x),
        static_cast<float>(value.y),
        static_cast<float>(value.z),
    };
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
    fileManager().addSearchPathFront("planet/resources");
    fileManager().addSearchPathFront("planet/resources/moon_textures");
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
        m_updateCB.ViewProjectionMatrix = glm::scale(PlanetMat4{1}, PlanetVec3{0});
        m_updateCB.InvViewProjectionMatrix = glm::scale(PlanetMat4{1}, PlanetVec3{0});
        m_Earth.update_constant_buffers(m_updateCB);
        m_MeshUpdater.reset_buffers(cmd, m_Earth.m_descriptorSet, m_Earth.m_CBTDescriptorSet);
        m_MeshUpdater.prepare_indirection(cmd, m_Earth);
        m_Earth.evaluate_leb(cmd, globalDescriptorSet, true, true);
        wataData.update_simulation(static_cast<float>(m_Time));
        wataData.upload_constant_buffers();
        m_WaterSimulation.evaluate(cmd, wataData);
        m_WaterDeformer.apply_deformation(cmd, m_Earth, wataData);

        m_MoonMaterial.upload_constant_buffers();
        m_Moon.update_constant_buffers(m_updateCB);
        m_MeshUpdater.reset_buffers(cmd, m_Moon.m_descriptorSet, m_Moon.m_CBTDescriptorSet);
        m_MeshUpdater.prepare_indirection(cmd, m_Moon);
        m_Moon.evaluate_leb(cmd, globalDescriptorSet, true, true);
        m_MoonDeformer.apply_deformation(cmd, m_Moon, m_MoonMaterial);
    });
    m_FrameIndex = 0;
}

void PlanetDemo::initGeometry() {
    // Num elements the CBT holds
    const auto cbtNumElements = cbt_large::cbt_num_elements(m_CBTType);
    cbt_large::CBT* cbt = create_cbt(m_CBTType);
    planetMesh = CPUMesh::load_cpu_mesh(resource("icosahedron.ccm"), cbtNumElements);
    m_LebMatrixCache.intialize(device, LEB_MATRIX_CACHE_SIZE);

    m_Earth = Planet{ { "earth", device, globalDescriptorSetLayout, g_EarthRadius, g_EarthCenter, g_EarthImpostorToggle, g_EarthTriangleSize, EARTH_MATERIAL } };
    m_Earth.initialize(*cbt, planetMesh);
    m_Earth.updateLEBDescriptorSet(m_LebMatrixCache.get_leb_matrix_buffer());

    wataData = { device };
    wataData.initialize();

    m_EarthRenderer = { { device, m_Earth, wataData, globalDescriptorSetLayout, textureDescriptorSetLayout, milkywayDescriptorSet } };
    m_EarthRenderer.initialize();

    m_Moon = Planet{ { "moon", device, globalDescriptorSetLayout, g_MoonRadius, g_MoonCenter, g_MoonImpostorToggle, g_MoonTriangleSize, MOON_MATERIAL } };
    m_Moon.initialize(*cbt, planetMesh);
    m_Moon.updateLEBDescriptorSet(m_LebMatrixCache.get_leb_matrix_buffer());

    m_MoonMaterial = { device };
    m_MoonMaterial.initialize(m_Moon);

    m_MoonRenderer = { { device, m_Moon, m_MoonMaterial, globalDescriptorSetLayout } };
    m_MoonRenderer.initialize();

    // TODO - move into planet and only create one CBTType shader, reload shader based on CBTType
    m_MeshUpdater = {device, globalDescriptorSetLayout};
    m_MeshUpdater.initialize(globalDescriptorSet);

    m_WaterSimulation = { device };
    m_WaterSimulation.initialize();

    m_WaterDeformer = { device };
    m_WaterDeformer.initialize();

    m_MoonDeformer = { device };
    m_MoonDeformer.initialize();


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
    camera.initialize(*this, { width, height});
}

void PlanetDemo::loadTextures() {
    textures::fromFile(device, milkyway, resource("milky_way/milky_way.png"), false, VK_FORMAT_R8G8B8A8_SRGB);
}

void PlanetDemo::newFrame() {
    camera.newFrame();
    updateAtmosphereInfo();
    updateConstantBuffers();
    if (earthIsUpdatable) {
        wataData.upload_constant_buffers();
    }
    m_MoonMaterial.upload_constant_buffers();
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
    std::array<VkDescriptorPoolSize, 5> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_SAMPLER, 100 * maxSets},
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
                    .fragmentShader(resource("skybox_atmosphere.frag.spv"))
                .vertexInputState().clear()
                    .addVertexBindingDescription(0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX)
                    .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
                .rasterizationState()
                    .cullFrontFace()
                .depthStencilState()
                    .compareOpLessOrEqual()
                .layout()
                    .addDescriptorSetLayout(textureDescriptorSetLayout)
                    .addDescriptorSetLayout(AppContext::uniformDescriptorSet())
                    .addDescriptorSetLayout(AppContext::atmosphere().descriptor.uboDescriptorSetLayout)
                    .addDescriptorSetLayout(AppContext::atmosphere().descriptor.lutDescriptorSetLayout)
                .name("skybox_render")
                .build(render.skybox.layout);

        render.skyboxMilkyway.pipeline =
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
                .name("skybox_milkyway_render")
                .build(render.skyboxMilkyway.layout);
    //    @formatter:on
}


void PlanetDemo::onSwapChainDispose() {
    dispose(render.primitive.pipeline);
    dispose(render.skybox.pipeline);
    dispose(render.skyboxMilkyway.pipeline);
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

    m_Earth.visibility(camera.position(), earthDistance, earthIsVisible, earthIsUpdatable);
    m_Moon.visibility(camera.position(), moonDistance, moonIsVisible, moonIsUpdatable);

    if (earthIsVisible) {
        m_WaterSimulation.evaluate(commandBuffer, wataData);
    }

    if (earthIsUpdatable) {
        m_MeshUpdater.update(commandBuffer, globalDescriptorSet, m_Earth);
        m_Earth.evaluate_leb(commandBuffer, globalDescriptorSet, m_RayTracingPath);
        m_WaterDeformer.apply_deformation(commandBuffer, m_Earth, wataData);
    }

    if (moonIsUpdatable) {
        m_MeshUpdater.update(commandBuffer, globalDescriptorSet, m_Moon);
        m_Moon.evaluate_leb(commandBuffer, globalDescriptorSet, m_RayTracingPath);
        m_MoonDeformer.apply_deformation(commandBuffer, m_Moon, m_MoonMaterial);
    }

    if (m_EnableValidation) {
        m_MeshUpdater.reset_validation(commandBuffer);
        m_MeshUpdater.validate(commandBuffer, m_Earth.m_CBTMesh, m_Earth.m_GeometryCB);
        m_MeshUpdater.validate(commandBuffer, m_Moon.m_CBTMesh, m_Moon.m_GeometryCB);
        m_MeshUpdater.resolve_validation(commandBuffer);
    }

    clearColor(0, 0, 1);

    renderToSwapChain([&]{
        m_EarthRenderer.render(commandBuffer, globalDescriptorSet, earthIsVisible);
        m_MoonRenderer.render(commandBuffer, globalDescriptorSet, moonIsVisible);
        renderSkyBox(commandBuffer);
        renderUI(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void PlanetDemo::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("controls");
    ImGui::SetWindowSize({200, 200});
    static bool play = true;
    if (ImGui::Button("advance")) {
       advanceFrame = true;
    } else {
        advanceFrame = play;
    }
    ImGui::SameLine();
    ImGui::Checkbox("play", &play);
    ImGui::Checkbox("wireframe", &m_ActiveWireFrame);
    ImGui::Checkbox("validation", &m_EnableValidation);
    ImGui::Checkbox("show water visualizer", &m_ShowWaterVisualizer);

    ImGui::End();

    ImGui::SetNextWindowSize(ImVec2(450.0f, 300.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Scene Parameters");
    {
        ImGui::TextUnformatted("General");
        ImGui::Separator();

        const char* currentCBTType = cbt_large::g_CBTTypesNames[static_cast<uint32_t>(m_NewCBTType)];
        if (ImGui::BeginCombo("CBT Type", currentCBTType)) {
            for (uint32_t n = 0; n < static_cast<uint32_t>(CBTType::Count); ++n) {
                const auto cbtType = static_cast<CBTType>(n);
                const bool isSelected = m_NewCBTType == cbtType;
                if (ImGui::Selectable(cbt_large::g_CBTTypesNames[n], isSelected)) {
                    m_NewCBTType = cbtType;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::TextUnformatted("Earth Water");
        ImGui::Separator();
        wataData.render_ui_global();
        wataData.render_ui_patch();
    }
    ImGui::End();

    if (m_ShowWaterVisualizer) {
        m_WaterSimulation.visualizer(plugin<ImGuiPlugin>(IM_GUI_PLUGIN));
    }

    camera.renderUI();
    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void PlanetDemo::renderSkyBox(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    const CameraT<float> skyboxCamera{
        .model = glm::mat4(camera.cameraMatrix().model),
        .view = glm::mat4(camera.cameraMatrix().view),
        .proj = glm::mat4(camera.cameraMatrix().proj),
    };

    if (earthIsVisible) {
        auto& atmosphere = AppContext::atmosphere();
        const std::array sets{
            milkywayDescriptorSet,
            atmosphere.info.descriptorSet,
            atmosphere.descriptor.uboDescriptorSet,
            atmosphere.descriptor.lutDescriptorSet,
        };

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.skybox.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.skybox.layout.handle, 0, sets.size(), sets.data(), 0, nullptr);
        vkCmdPushConstants(commandBuffer, render.skybox.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(skyboxCamera), &skyboxCamera);
    } else {
        const std::array sets{ milkywayDescriptorSet };

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.skyboxMilkyway.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.skyboxMilkyway.layout.handle, 0, sets.size(), sets.data(), 0, nullptr);
        vkCmdPushConstants(commandBuffer, render.skyboxMilkyway.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(skyboxCamera), &skyboxCamera);
    }

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, skybox.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, skybox.indexes, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, skybox.indexes.sizeAs<uint32_t>(), 1, 0, 0, 0);
}

void PlanetDemo::updateAtmosphereInfo() {
    auto& info = *AppContext::atmosphere().info.cpu;
    const auto& cam = camera.cameraMatrix();
    info.inverse_model = glm::inverse(glm::mat4(cam.model));
    info.inverse_view = glm::inverse(glm::mat4(cam.view));
    info.inverse_projection = glm::inverse(glm::mat4(cam.proj));
    info.camera = glm::vec4(toAtmosphereVec3(camera.position()), 1.0f);
    info.earthCenter = glm::vec4(toAtmosphereVec3(PlanetVec3(g_EarthCenter)), 1.0f);

    auto sinPhi = glm::sin(-m_SunRotation);
    auto cosPhi = glm::cos(-m_SunRotation);
    auto sinTheta = glm::sin(-m_SunElevation);
    auto cosTheta = glm::cos(-m_SunElevation);
    auto sunDirection = glm::normalize(glm::vec3{ sinTheta * sinPhi, cosTheta, sinTheta * cosPhi });
    info.sunDirection = glm::vec4{sunDirection, 1};
}

void PlanetDemo::update(float time) {
    if (!ImGui::IsAnyItemActive()) {
        camera.update(time);
    }
    if (earthIsUpdatable) {
        wataData.update_simulation(time);
    }
    setTitle(fmt::format("{}, camera : {}, view: {}, fps - {}", title, camera.position(), camera.viewDirection(), framePerSecond));
}

void PlanetDemo::checkAppInputs() {
    camera.processInput();
}

void PlanetDemo::endFrame() {
    if (m_EnableValidation) {
        vkQueueWaitIdle(device.queues.graphics);
        assert(m_MeshUpdater.check_if_valid() && "Validation failed.");
    }

    if (m_MirrorPOV) {
        
        camera.get(view, viewProjection, invViewProjection, frustum);
        m_updateCB.ViewProjectionMatrix = viewProjection;
        m_updateCB.InvViewProjectionMatrix = invViewProjection;
        m_updateCB.CameraPosition = camera.position();
        m_updateCB.CameraForward = -PlanetVec3(view[0][2], view[1][2], view[2][2]);
        m_updateCB.FarPlaneDistance = camera.far();
        m_updateCB.FOV = glm::radians(camera.fieldOfView() * PlanetScalar(0.5));
        std::memcpy(m_updateCB.FrustumPlanes.data(), frustum.cp.data(), BYTE_SIZE(frustum.cp));

        m_Earth.update_constant_buffers(m_updateCB);
        m_Moon.update_constant_buffers(m_updateCB);
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
    camera.get(view, viewProjection, invViewProjection, frustum);
    
    global->ViewProjectionMatrix = viewProjection;
    global->InvViewProjectionMatrix = invViewProjection;
    global->CameraPosition = camera.position();
    global->FoV = glm::radians(camera.fieldOfView() * PlanetScalar(0.5));
    global->ScreenSize = PlanetVec2{static_cast<PlanetScalar>(width), static_cast<PlanetScalar>(height)};
    global->SunDirection = AppContext::AtmosphereInfo().sunDirection;
    global->FrameIndex = m_FrameIndex;
    global->Time = elapsedTime;
    global->FarPlaneDistance = camera.far();
    global->WireFrameColor = PlanetVec3(m_WireframeColor);
    global->WireFrameSize = m_ActiveWireFrame ? m_WireframeSize : 0.0f;
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
