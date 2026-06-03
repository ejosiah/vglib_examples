#pragma once


// Project includes
#include "cbt/large/bisector.h"
#include "cbt/large/cbt_utility.h"
#include "cpu_mesh.hpp"
#include <VulkanDevice.h>


struct BaseMesh {
    // Number of vertices of the mesh
    uint32_t numVertices;
    // Vertex buffer
    VulkanBuffer vertexBuffer;

    // Num primitives
    uint32_t numElements;
    // Index buffer
    VulkanBuffer indexBuffer;
};

struct CBTMesh {
    friend class MeshUpdater;
    friend class WaterDeformer;
    friend class EarthRenderer;
    friend class MoonRenderer;
    // CBT variant backing this mesh
    CBTType cbtType{CBTType::OCBT_128K};

    // Total number of elements
    uint32_t totalNumElements;

    // The base number of vertices
    uint32_t numBaseVertices;

    // Base heap depth
    uint32_t baseDepth;

    // Bisector buffers
    VulkanBuffer heapIDBuffer;
    uint32_t currentNeighborsBufferIdx;
    VulkanBuffer neighborsBuffers[2];

    // Intermediate buffers
    VulkanBuffer updateBuffer;
    VulkanBuffer classificationBuffer;
    VulkanBuffer simplificationBuffer;
    VulkanBuffer allocateBuffer;
    VulkanBuffer propagateBuffer;

    // Indexation buffers
    VulkanBuffer indirectDrawBuffer;
    VulkanBuffer indirectDispatchBuffer;
    VulkanBuffer indexedBisectorBuffer;
    VulkanBuffer visibleIndexedBisectorBuffer;
    VulkanBuffer modifiedIndexedBisectorBuffer;

    // Geometry buffers
    VulkanBuffer lebVertexBuffer;
    VulkanBuffer currentVertexBuffer;
    VulkanBuffer currentDisplacementBuffer;

    // Device version of the CBT
    GPU_CBT gpuCBT;

    // Descriptor set containing the mesh buffers
    VkDescriptorSet descriptorSet{};

    // Descriptor set containing the CBT buffers
    VkDescriptorSet cbtDescriptorSet{};
};

// Function to initialize a cbt mesh
void initialize_cbt_mesh(const CPUMesh& cpuMesh, const CBT& cbt, VulkanDevice& device, CBTMesh& cbtMesh);

// Function to initialize a base mesh
void initialize_base_mesh(const CPUMesh& cpuMesh, VulkanDevice& device, BaseMesh& baseMesh);
