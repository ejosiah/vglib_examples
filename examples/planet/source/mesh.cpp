#include "mesh.hpp"

#include <stdexcept>
#include <vector>

namespace {

constexpr VkBufferUsageFlags StorageBufferUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
constexpr VkBufferUsageFlags IndirectStorageBufferUsage = StorageBufferUsage | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
constexpr VkBufferUsageFlags VertexStorageBufferUsage = StorageBufferUsage | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
constexpr VkBufferUsageFlags IndexStorageBufferUsage = StorageBufferUsage | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

template <typename T>
VkDeviceSize buffer_size(uint32_t elementCount) {
    return sizeof(T) * elementCount;
}

VulkanBuffer create_storage_buffer(VulkanDevice& device, VkDeviceSize byteSize, VkBufferUsageFlags usage = StorageBufferUsage) {
    return device.createBuffer(usage, VMA_MEMORY_USAGE_GPU_ONLY, byteSize);
}

bool supports_shader_float64(VulkanDevice& device) {
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(device.physicalDevice, &features);
    return features.shaderFloat64 == VK_TRUE;
}

CBTType cbt_type_from_num_elements(uint32_t numElements) {
    if (numElements == cbt_large::cbt_num_elements(CBTType::OCBT_128K)) return CBTType::OCBT_128K;
    if (numElements == cbt_large::cbt_num_elements(CBTType::OCBT_256K)) return CBTType::OCBT_256K;
    if (numElements == cbt_large::cbt_num_elements(CBTType::OCBT_512K)) return CBTType::OCBT_512K;
    if (numElements == cbt_large::cbt_num_elements(CBTType::OCBT_1M)) return CBTType::OCBT_1M;
    throw std::runtime_error("unsupported CBT size");
}

}

void initialize_cbt_mesh(const CPUMesh &cpuMesh, const CBT &cbt, VulkanDevice &device, CBTMesh &cbtMesh) {
    cbtMesh.cbtType = cbt_type_from_num_elements(cbt.num_elements());
    cbt_large::initialize_gpu_cbt(cbt, device, cbtMesh.gpuCBT);

    cbtMesh.totalNumElements = cpuMesh.totalNumElements;
    cbtMesh.numBaseVertices = static_cast<uint32_t>(cpuMesh.basePoints.size());
    cbtMesh.baseDepth = cpuMesh.minimalDepth;

    cbtMesh.heapIDBuffer = device.createDeviceLocalBuffer(cpuMesh.heapIDArray.data(), buffer_size<uint64_t>(cpuMesh.totalNumElements), StorageBufferUsage);

    cbtMesh.currentNeighborsBufferIdx = 0;
    cbtMesh.neighborsBuffers[0] = device.createDeviceLocalBuffer(cpuMesh.neighborsArray.data(), buffer_size<glm::uvec3>(cpuMesh.totalNumElements), StorageBufferUsage);
    cbtMesh.neighborsBuffers[1] = create_storage_buffer(device, buffer_size<glm::uvec3>(cpuMesh.totalNumElements));

    cbtMesh.updateBuffer = create_storage_buffer(device, buffer_size<cbt_large::BisectorData>(cpuMesh.totalNumElements));
    cbtMesh.classificationBuffer = create_storage_buffer(device, buffer_size<uint32_t>(2 + cpuMesh.totalNumElements * 2));
    cbtMesh.simplificationBuffer = create_storage_buffer(device, buffer_size<uint32_t>(1 + cpuMesh.totalNumElements));
    cbtMesh.allocateBuffer = create_storage_buffer(device, buffer_size<uint32_t>(1 + cpuMesh.totalNumElements));
    cbtMesh.propagateBuffer = create_storage_buffer(device, buffer_size<uint32_t>(2 + cpuMesh.totalNumElements));

    cbtMesh.indirectDrawBuffer = create_storage_buffer(device, buffer_size<uint32_t>(4 * 2 + 2), IndirectStorageBufferUsage);
    cbtMesh.indirectDispatchBuffer = create_storage_buffer(device, buffer_size<uint32_t>(3 * 3), IndirectStorageBufferUsage);
    cbtMesh.indexedBisectorBuffer = create_storage_buffer(device, buffer_size<uint32_t>(cpuMesh.totalNumElements));
    cbtMesh.visibleIndexedBisectorBuffer = create_storage_buffer(device, buffer_size<uint32_t>(cpuMesh.totalNumElements));
    cbtMesh.modifiedIndexedBisectorBuffer = create_storage_buffer(device, buffer_size<uint32_t>(cpuMesh.totalNumElements));

    auto byteSize = supports_shader_float64(device) ? buffer_size<glm::dvec3>(cbtMesh.totalNumElements * 4): buffer_size<glm::vec3>(cbtMesh.totalNumElements * 4);
    cbtMesh.lebVertexBuffer = create_storage_buffer(device, byteSize);
    cbtMesh.currentVertexBuffer = create_storage_buffer(device, buffer_size<glm::vec3>(cbtMesh.totalNumElements * 4), VertexStorageBufferUsage);
    cbtMesh.currentDisplacementBuffer = create_storage_buffer(device, buffer_size<glm::vec3>(cbtMesh.totalNumElements * 3));
}

void initialize_base_mesh(const CPUMesh &cpuMesh, VulkanDevice &device, BaseMesh &baseMesh) {
    const auto numBaseVertices = static_cast<uint32_t>(cpuMesh.basePoints.size());
    baseMesh.numVertices = numBaseVertices;
    baseMesh.numElements = numBaseVertices / 3;

    baseMesh.vertexBuffer = device.createDeviceLocalBuffer(cpuMesh.basePoints.data(), buffer_size<glm::vec3>(numBaseVertices), VertexStorageBufferUsage);

    std::vector<glm::uvec3> indices(cpuMesh.totalNumElements);
    for (uint32_t elementIdx = 0; elementIdx < cpuMesh.totalNumElements; ++elementIdx) {
        indices[elementIdx] = { 3 * elementIdx, 3 * elementIdx + 1, 3 * elementIdx + 2 };
    }

    baseMesh.indexBuffer = device.createDeviceLocalBuffer(indices.data(), buffer_size<glm::uvec3>(cpuMesh.totalNumElements), IndexStorageBufferUsage);
}
