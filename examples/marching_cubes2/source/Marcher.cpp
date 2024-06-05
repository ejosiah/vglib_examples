#include "Marcher.hpp"
#include "MarchingCubeLuts.hpp"
#include "AppContext.hpp"
#include <spdlog/spdlog.h>
#include "Vertex.h"
#include <meshoptimizer.h>
#include <unordered_map>
#include "Barrier.hpp"

Marcher::Marcher(Voxels *voxels, float minGridSize)
: ComputePipelines(&AppContext::device())
, _voxels(voxels)
, _minGridSize(minGridSize)
, _constants{
    .bMin = voxels->bounds.min,
    .bMax = voxels->bounds.max,
}
{}

void Marcher::init() {
    initBuffers();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createPipelines();
}

Marcher::Mesh Marcher::generateMesh(float gridSize) {
    device->graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        generateMesh(commandBuffer, gridSize);
    });
//    VulkanBuffer vertexStagingBuffer = device->createStagingBuffer(numVertices * sizeof(glm::vec4));
//    VulkanBuffer normalStagingBuffer = device->createStagingBuffer(numVertices * sizeof(glm::vec4));
//
//    device->graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
//        VkBufferCopy region{0, 0, vertexStagingBuffer.size};
//        vkCmdCopyBuffer(commandBuffer, _vertices, vertexStagingBuffer, 1, &region);
//        vkCmdCopyBuffer(commandBuffer, _normals, normalStagingBuffer, 1, &region);
//    });
//
//    std::vector<Vertex> vertices{};
//    vertices.reserve(numVertices);
//
//    std::span<glm::vec3> vs = { reinterpret_cast<glm::vec3*>(vertexStagingBuffer.map()), numVertices };
//    std::span<glm::vec3> ns = { reinterpret_cast<glm::vec3*>(normalStagingBuffer.map()), numVertices };
//
//
//    for(int i = 0; i < numVertices; ++i){
//        Vertex v{};
//        v.color = {1, 1, 0, 1};
//        v.position = glm::vec4{ vs[i].xyz(), 1 };
//        v.normal = ns[i].xyz();
//        vertices.push_back(v);
//    }
//    std::vector<uint32_t> oindices(vertices.size());
//    std::iota(oindices.begin(), oindices.end(), 0);
//    auto [indices, newVertices] = generateIndices(vertices, gridSize * 0.25f);

//    drawCmd->indexCount = oindices.size();
//    drawCmd->instanceCount = 1;
//    drawCmd->firstIndex = 0;
//    drawCmd->vertexOffset = 0;
//    drawCmd->firstInstance = 0;
//    _drawCmd.unmap();

    return  _mesh;
}

void Marcher::generateMesh(VkCommandBuffer commandBuffer, float gridSize) {
    _constants.gridSize = gridSize;

    static std::array<VkDescriptorSet, 3> sets{};
    sets[0] = _voxels->descriptorSet;
    sets[1] = _lutDescriptorSet;
    sets[2] = _vertexDescriptorSet;

    glm::uvec3 gc{ ((_voxels->bounds.max - _voxels->bounds.min) + _minGridSize)/_minGridSize };
    gc /= 8;
    VkDrawIndexedIndirectCommand drawCmd{0, 1, 0, 0, 0};

    vkCmdFillBuffer(commandBuffer, _mesh.vertexCount, 0, sizeof(uint32_t), 0);
    vkCmdUpdateBuffer(commandBuffer, _mesh.drawCmd, 0, sizeof(drawCmd), &drawCmd);
    Barrier::transferWriteToComputeWrite(commandBuffer, _mesh.drawCmd);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("marching_cubes"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("marching_cubes"), 0, sets.size(), sets.data(), 0, nullptr);
    vkCmdPushConstants(commandBuffer, layout("marching_cubes"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(_constants), &_constants);
    vkCmdDispatch(commandBuffer, gc.x, gc.y,  gc.z);

}


std::vector<PipelineMetaData> Marcher::pipelineMetaData() {
    return {
            {
                "marching_cubes",
                AppContext::resource("marching_cubes.comp.spv"),
                {&_voxels->descriptorSetLayout, &_lutDescriptorSetLayout, &_vertexDescriptorSetLayout},
                {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(_constants)}}
            }
    };
}

void Marcher::initBuffers() {
    VkDeviceSize size = (20 << 20); // 20 mb

    _edgeLUT = device->createDeviceLocalBuffer(EdgeTable.data(), BYTE_SIZE(EdgeTable), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    _triangleLUT = device->createDeviceLocalBuffer(TriangleTable.data(), BYTE_SIZE(TriangleTable), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    _mesh.vertices = device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_GPU_ONLY, size * sizeof(Vertex));
    _mesh.indices = device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_GPU_ONLY, size);
    _mesh.drawCmd = device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(VkDrawIndexedIndirectCommand));
    _mesh.vertexCount = device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(uint32_t));
    _mesh.numVertices = reinterpret_cast<uint32_t*>(_mesh.vertexCount.map());
}

void Marcher::createDescriptorSetLayouts() {
    _lutDescriptorSetLayout =
        device->descriptorSetLayoutBuilder()
            .name("mc_lut_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    _vertexDescriptorSetLayout =
        device->descriptorSetLayoutBuilder()
            .name("mc_vertex_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

}

void Marcher::updateDescriptorSets() {
    auto sets =  AppContext::allocateDescriptorSets({ _lutDescriptorSetLayout, _vertexDescriptorSetLayout });
    _lutDescriptorSet = sets[0];
    _vertexDescriptorSet = sets[1];
    
    auto writes = initializers::writeDescriptorSets<6>();
    
    writes[0].dstSet = _lutDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo edgeInfo{ _edgeLUT, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &edgeInfo;

    writes[1].dstSet = _lutDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo triInfo{ _triangleLUT, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &triInfo;

    writes[2].dstSet = _vertexDescriptorSet;
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo meshVertexInfo{ _mesh.vertices, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &meshVertexInfo;

    writes[3].dstSet = _vertexDescriptorSet;
    writes[3].dstBinding = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    VkDescriptorBufferInfo meshIndexInfo{ _mesh.indices, 0, VK_WHOLE_SIZE };
    writes[3].pBufferInfo = &meshIndexInfo;

    writes[4].dstSet = _vertexDescriptorSet;
    writes[4].dstBinding = 2;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    VkDescriptorBufferInfo meshVertexCountInfo{ _mesh.vertexCount, 0, VK_WHOLE_SIZE };
    writes[4].pBufferInfo = &meshVertexCountInfo;

    writes[5].dstSet = _vertexDescriptorSet;
    writes[5].dstBinding = 3;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = 1;
    VkDescriptorBufferInfo meshDrawInfo{ _mesh.drawCmd, 0, VK_WHOLE_SIZE };
    writes[5].pBufferInfo = &meshDrawInfo;

    device->updateDescriptorSets(writes);

}

VkDeviceSize Marcher::computeVertxBufferSize() const {
    glm::ivec3 dim{ (_voxels->bounds.max - _voxels->bounds.min)/_minGridSize };
    VkDeviceSize size = dim.x * dim.y * dim.z * sizeof(glm::vec3) * MaxTriangleVertices;

    return size;
}

struct IndexedVertex{
    glm::vec3 position;
    uint32_t oldIndex;
    uint32_t newIndex;
};

std::tuple<std::vector<uint32_t>, std::vector<Vertex>>
Marcher::generateIndices(std::vector<Vertex> vertices, float threshold) {
    std::vector<uint32_t> indices(vertices.size());
    std::iota(indices.begin(), indices.end(), 0);

    std::vector<IndexedVertex> indexedVertices;
    indexedVertices.reserve(vertices.size());
    for(uint32_t i = 0; i < vertices.size(); ++i) {
        indexedVertices.push_back({ vertices[i].position, i, i });
    }

    std::sort(indexedVertices.begin(), indexedVertices.end(), [&](const auto& va, const auto& vb) {
       auto da = glm::distance(va.position, _voxels->bounds.min);
       auto db = glm::distance(vb.position, _voxels->bounds.min);
       return da < db;
    });

    for(auto i = 1u; i < indexedVertices.size(); ++i) {
        auto& va = indexedVertices[i - 1];
        auto& vb = indexedVertices[i];

        if(glm::all(glm::epsilonEqual(va.position, vb.position, threshold))){
            vb.newIndex = va.newIndex;
        }
    }

    std::array<Vertex, 3> triangle{};
    for(const auto& iv : indexedVertices) {
        indices[iv.oldIndex] = iv.newIndex;
    }

    std::vector<Vertex> newVertices{};

    std::set<uint32_t> visited;
    std::vector<uint32_t> newIndices;
    std::unordered_map<uint32_t, uint32_t> indexMap{};

    for(const auto& index : indices) {
        if(indexMap.contains(index)){
            newIndices.push_back(indexMap[index]);
            continue;
        }
        newVertices.push_back(vertices[index]);
        uint32_t nIndex = newVertices.size() - 1;
        newIndices.push_back(nIndex);
        indexMap[index] = nIndex;
    }

    spdlog::info("{} vertices generated after indexing", newVertices.size());

    return std::make_tuple(newIndices, newVertices);
}

