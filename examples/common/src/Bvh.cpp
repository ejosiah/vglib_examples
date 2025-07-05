#include "gltf/Bvh.hpp"
#include "gltf/GltfLoader.hpp"
#include "Vertex.h"
#include "Barrier.hpp"

gltf::bvh::Bvh::Bvh(VulkanDevice& device, VulkanDescriptorPool& descriptorPool, std::shared_ptr<gltf::Model> model)
: m_device(&device)
, m_descriptorPool(&descriptorPool)
, m_model(std::move(model))
{}

gltf::bvh::Bvh::~Bvh() {
    if(m_tlas.handle) {
        vkDestroyAccelerationStructureKHR(device(), m_tlas.handle, nullptr);
    }
    for(auto& blas : m_blas) {
        vkDestroyAccelerationStructureKHR(device(), blas.handle, nullptr);
    }
}

void gltf::bvh::Bvh::build() {
    createBlas();
    createTlas();
    createDescriptorSet();
}

void gltf::bvh::Bvh::createBlas() {
    auto size = std::max(m_model->draw.u8.handle.size, std::max(m_model->draw.u16.handle.size, m_model->draw.u32.handle.size));
    auto staging = device().createStagingBuffer(size);
    
    auto u8 = createBlas(staging, m_model->draw.u8, m_model->vertices, m_model->indices.u8.handle, IndexType::U8);
    auto u16 = createBlas(staging, m_model->draw.u16, m_model->vertices, m_model->indices.u16.handle, IndexType::U16);
    auto u32 = createBlas(staging, m_model->draw.u32, m_model->vertices, m_model->indices.u32.handle, IndexType::U32);

    m_blasOffset.u8 = 0;
    m_instanceMeshMap.u8 = std::get<0>(u8);
    m_blas.insert(m_blas.end(), std::get<1>(u8).begin(), std::get<1>(u8).end());

    m_blasOffset.u16 = m_blas.size();
    m_instanceMeshMap.u16 = std::get<0>(u16);
    m_blas.insert(m_blas.end(), std::get<1>(u16).begin(), std::get<1>(u16).end());

    m_blasOffset.u32 = m_blas.size();
    m_instanceMeshMap.u32 = std::get<0>(u32);
    m_blas.insert(m_blas.end(), std::get<1>(u32).begin(), std::get<1>(u32).end());
    
}

void ensureAlignmentScratchBufferSize(VulkanDevice& device, VkAccelerationStructureBuildSizesInfoKHR &info)  {
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 props{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &asProps };
    vkGetPhysicalDeviceProperties2(device, &props);
    info.buildScratchSize = alignedSize(info.buildScratchSize, asProps.minAccelerationStructureScratchOffsetAlignment);
}

VkPhysicalDeviceAccelerationStructurePropertiesKHR getAccelerationStructureProperties(VulkanDevice& device) {
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 props{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &asProps };
    vkGetPhysicalDeviceProperties2(device, &props);
    return asProps;
}

gltf::bvh::ScratchBuffer  createScratchBuffer(VulkanDevice& device, VkDeviceSize size) {
    gltf::bvh::ScratchBuffer scratchBuffer{};
    const auto alignment = getAccelerationStructureProperties(device).minAccelerationStructureScratchOffsetAlignment;

    scratchBuffer.handle = device.createAlignedBuffer(
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            size, alignment, "acceleration_struct_scratch_buffer");

    VkBufferDeviceAddressInfo bufferDeviceAddressInfo{};
    bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bufferDeviceAddressInfo.buffer = scratchBuffer.handle;
    scratchBuffer.address = vkGetBufferDeviceAddress(device, &bufferDeviceAddressInfo);

    return scratchBuffer;
}

std::tuple<std::map<int, int>, std::vector<gltf::bvh::Blas>> gltf::bvh::Bvh::createBlas(VulkanBuffer& staging, gltf::DrawGroup &draw,
                                                                                        VulkanBuffer& vertices, VulkanBuffer& indexes,
                                                                                        IndexType indexType) {
    if(draw.count <= 0) return {};

    std::vector<VkDrawIndexedIndirectCommand> meshes;

    device().copy(draw.handle, staging, draw.handle.size);

    auto isSame = [](auto& a, auto& b){ return std::memcmp(&a, &b, sizeof(a)) == 0; };

    std::map<int, int> imap;
    auto view = staging.span<VkDrawIndexedIndirectCommand>(draw.count);
    auto instanceCount = 0;
    for(auto cmd : view) {
        if(cmd.instanceCount == 0) continue;
        auto itr = std::find_if(meshes.begin(), meshes.end(), [&](auto& mesh){ return isSame(mesh, cmd); });
        if(itr != meshes.end()) {
            auto index = std::distance(meshes.begin(), itr);
            imap[instanceCount] = index;
        }else {
            imap[instanceCount] = meshes.size();
            meshes.push_back(cmd);
        }
        instanceCount++;
    }

    VkDeviceOrHostAddressConstKHR vertexAddress{ .deviceAddress = device().getAddress(vertices) };
    VkDeviceOrHostAddressConstKHR indexAddress{ .deviceAddress =  device().getAddress(indexes) };

    auto toVkIndexType = [](auto iType){
        switch(iType){
            case IndexType::U8: return std::make_tuple(VK_INDEX_TYPE_UINT8_KHR, sizeof(uint8_t));
            case IndexType::U16: return std::make_tuple(VK_INDEX_TYPE_UINT16, sizeof(uint16_t));
            case IndexType::U32: return std::make_tuple(VK_INDEX_TYPE_UINT32, sizeof(uint32_t));
            default: throw std::invalid_argument{ "invalid index type" };
        }
    };

    const auto meshCount = meshes.size();

    auto numVertices = [&](auto index) -> size_t {
        if(index + 1 < meshCount) {
            return meshes[index+1].vertexOffset - meshes[index].vertexOffset;
        }else {
            return vertices.sizeAs<VertexMultiAttributes>() - meshes[index].vertexOffset;
        }
    };

    std::vector<VkAccelerationStructureGeometryKHR> geometries{};
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangeInfos{};

    geometries.reserve(meshCount);
    buildRangeInfos.reserve(meshCount);

    for(auto i = 0; i < meshCount; ++i) {
        auto& mesh = meshes[i];
        auto& geometry = geometries.emplace_back();
        auto [vkIndexType, indexSize] = toVkIndexType(indexType);

        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        geometry.geometry.triangles.vertexData = vertexAddress;
        geometry.geometry.triangles.vertexStride = sizeof(VertexMultiAttributes);
        geometry.geometry.triangles.maxVertex = numVertices(i) - 1;
        geometry.geometry.triangles.indexType = vkIndexType;
        geometry.geometry.triangles.indexData = indexAddress;
        geometry.geometry.triangles.transformData.deviceAddress = 0;
        geometry.geometry.triangles.transformData.hostAddress = nullptr;

        auto& buildRange = buildRangeInfos.emplace_back();
        buildRange.primitiveCount = mesh.indexCount / 3;
        buildRange.primitiveOffset = mesh.firstIndex * indexSize;
        buildRange.firstVertex = mesh.vertexOffset;
        buildRange.transformOffset = 0;
    }

    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> buildGeometryInfos{};

    for(auto i = 0; i < meshCount; ++i){
        auto& buildGeometryInfo = buildGeometryInfos.emplace_back();
        buildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        buildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR ;
        buildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildGeometryInfo.geometryCount = 1;
        buildGeometryInfo.pGeometries = &geometries[i];
    }


    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    std::vector<gltf::bvh::Blas> blasCollection;

    for(auto i = 0; i < meshCount; ++i) {
        VkAccelerationStructureBuildSizesInfoKHR tempSize{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        vkGetAccelerationStructureBuildSizesKHR(device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                &buildGeometryInfos[i], &buildRangeInfos[i].primitiveCount, &tempSize);


        auto& blas = blasCollection.emplace_back();
        blas.buffer = device().createBuffer(usage, VMA_MEMORY_USAGE_GPU_ONLY, tempSize.accelerationStructureSize);
        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = blas.buffer;
        createInfo.size = blas.buffer.size;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        ERR_GUARD_VULKAN(vkCreateAccelerationStructureKHR(device(), &createInfo, nullptr, &blas.handle));

        sizeInfo.accelerationStructureSize = std::max(sizeInfo.accelerationStructureSize, tempSize.accelerationStructureSize);
        sizeInfo.buildScratchSize = std::max(sizeInfo.buildScratchSize, tempSize.buildScratchSize);
        sizeInfo.updateScratchSize = std::max(sizeInfo.updateScratchSize, tempSize.updateScratchSize);
    }

    ensureAlignmentScratchBufferSize(device(), sizeInfo);
    auto scratchBuffer = createScratchBuffer(device(), sizeInfo.buildScratchSize);

    for(auto i = 0; i < meshCount; ++i) {
        auto& info = buildGeometryInfos[i];
        info.dstAccelerationStructure = blasCollection[i].handle;
        info.scratchData.deviceAddress = scratchBuffer.address;
    }

    auto buildRangeInfoPtr = map_range(buildRangeInfos, [](const auto& info){ return &info; });

    Synchronization sync{};
    sync._fence = device().createFence();
    sync._fence.reset();

    device().graphicsCommandPool().oneTimeCommands(meshCount, [&](auto index, auto commandBuffer){
        vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildGeometryInfos[index], &buildRangeInfoPtr[index]);

        Barriers::push(VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                       VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR);
        Barriers::flush(commandBuffer);
    }, sync);

    for(auto i = 0; i < meshCount; ++i) {
        VkAccelerationStructureDeviceAddressInfoKHR asDeviceAddressInfo{};
        asDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        asDeviceAddressInfo.accelerationStructure = blasCollection[i].handle;
        blasCollection[i].deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device(), &asDeviceAddressInfo);
    }

    return std::make_tuple(imap, blasCollection);

}

VulkanDevice &gltf::bvh::Bvh::device() {
    return *m_device;
}

void gltf::bvh::Bvh::createTlas() {
    auto size = std::max(m_model->meshes.u8.handle.size, std::max(m_model->meshes.u16.handle.size, m_model->meshes.u32.handle.size));
    auto staging = device().createStagingBuffer(size);

    auto u8 = createInstances(staging, m_model->meshes.u8.handle, m_instanceMeshMap.u8, m_blasOffset.u8, 0);
    auto u16 = createInstances(staging, m_model->meshes.u16.handle, m_instanceMeshMap.u16, m_blasOffset.u16, 1);
    auto u32 = createInstances(staging, m_model->meshes.u32.handle, m_instanceMeshMap.u32, m_blasOffset.u32, 2);

    std::vector<VkAccelerationStructureInstanceKHR> inst{};
    inst.insert(inst.end(), u8.begin(), u8.end());
    inst.insert(inst.end(), u16.begin(), u16.end());
    inst.insert(inst.end(), u32.begin(), u32.end());

    VkBufferUsageFlags rtxUsage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;


    auto numInstances = COUNT(inst);
    auto instances = device().createDeviceLocalBuffer(inst.data(), BYTE_SIZE(inst), rtxUsage);
    device().setName<VK_OBJECT_TYPE_BUFFER>("rtx_instances", instances.buffer);

    VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
    instanceDataDeviceAddress.deviceAddress = device().getAddress(instances);

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType =  VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data = instanceDataDeviceAddress;

    VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo{};
    buildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildGeometryInfo.geometryCount = 1;
    buildGeometryInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizesInfo{};
    sizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(
            device(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildGeometryInfo,
            &numInstances,
            &sizesInfo);

    m_tlas.buffer = device().createBuffer(
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
            VMA_MEMORY_USAGE_GPU_ONLY,
            sizesInfo.accelerationStructureSize
    );
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = m_tlas.buffer;
    createInfo.size = sizesInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    vkCreateAccelerationStructureKHR(device(), &createInfo, nullptr, &m_tlas.handle);

    ensureAlignmentScratchBufferSize(device(), sizesInfo);
    auto scratchBuffer = createScratchBuffer(device(), sizesInfo.buildScratchSize);

    buildGeometryInfo.mode =  VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildGeometryInfo.srcAccelerationStructure = VK_NULL_HANDLE;
    buildGeometryInfo.dstAccelerationStructure = m_tlas.handle;
    buildGeometryInfo.scratchData.deviceAddress = scratchBuffer.address;

    VkAccelerationStructureBuildRangeInfoKHR  accelerationStructureBuildRangeInfo{};
    accelerationStructureBuildRangeInfo.primitiveCount = numInstances;
    accelerationStructureBuildRangeInfo.primitiveOffset = 0;
    accelerationStructureBuildRangeInfo.firstVertex = 0;
    accelerationStructureBuildRangeInfo.transformOffset = 0;
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };

    Synchronization sync{};
    sync._fence = device().createFence();
    sync._fence.reset();

    device().computeCommandPool().oneTimeCommand( [&](auto commandBuffer){
        vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildGeometryInfo, accelerationBuildStructureRangeInfos.data());
    }, sync);

    sync._fence.wait();

    VkAccelerationStructureDeviceAddressInfoKHR accelerationStructureDeviceAddressInfo{};
    accelerationStructureDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    accelerationStructureDeviceAddressInfo.accelerationStructure = m_tlas.handle;
    m_tlas.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device(), &accelerationStructureDeviceAddressInfo);
}

std::vector<VkAccelerationStructureInstanceKHR> gltf::bvh::Bvh::createInstances(const VulkanBuffer &staging, VulkanBuffer& instanceBuffer,
                                                                                std::map<int, int>& instanceBlasMap, size_t blasOffset, uint32_t customIndex) {

    device().copy(instanceBuffer, staging, instanceBuffer.size);
    auto instanceCount = instanceBuffer.sizeAs<gltf::MeshData>() - 1;   // we always have one extra unused instance

    if(instanceCount <= 0) return {};

    auto instances = staging.span<gltf::MeshData>(instanceCount);
    std::vector<VkAccelerationStructureInstanceKHR> asInstances{};

    for(auto i = 0; i < instanceCount; ++i) {
        auto& meshInstance = instances[i];
        auto& instance = asInstances.emplace_back();
        auto xform = glm::transpose(meshInstance.model);
        std::memcpy(&instance.transform, glm::value_ptr(xform), sizeof(VkTransformMatrixKHR));
        instance.instanceCustomIndex =  (i << 2) | customIndex;    // draw_id:22 | customIndex:2
        instance.mask = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

        auto blasIndex = instanceBlasMap[i] + blasOffset;
        instance.accelerationStructureReference = m_blas[blasIndex].deviceAddress;
    }
    return asInstances;
}

void gltf::bvh::Bvh::createDescriptorSet() {
    assert(rtxDescriptorSetLayout.handle && "Bvh::createDescriptorSetLayout() should be run before createDescriptorSet");

    m_model->rtxDescriptorSet = m_descriptorPool->allocate({ rtxDescriptorSetLayout }).front();

    auto writes = initializers::writeDescriptorSets<5>();

    VkWriteDescriptorSetAccelerationStructureKHR accWrites{};
    accWrites.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    accWrites.accelerationStructureCount = 1;
    accWrites.pAccelerationStructures = &m_tlas.handle;

    writes[0].pNext = &accWrites;
    writes[0].dstSet = m_model->rtxDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    writes[1].dstSet = m_model->rtxDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    VkDescriptorBufferInfo vertexInfo{ m_model->vertices, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &vertexInfo;

    std::vector<VkDescriptorBufferInfo> indexInfo {
            { m_model->indices.u8.handle, 0, VK_WHOLE_SIZE },
            { m_model->indices.u16.handle, 0, VK_WHOLE_SIZE },
            { m_model->indices.u32.handle, 0, VK_WHOLE_SIZE },
    };

    writes[2].dstSet = m_model->rtxDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 3;
    writes[2].pBufferInfo = indexInfo.data();

    std::vector<VkDescriptorBufferInfo> meshInfo {
            { m_model->meshes.u8.handle, 0, VK_WHOLE_SIZE },
            { m_model->meshes.u16.handle, 0, VK_WHOLE_SIZE },
            { m_model->meshes.u32.handle, 0, VK_WHOLE_SIZE },
    };

    writes[3].dstSet = m_model->rtxDescriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 3;
    writes[3].pBufferInfo = meshInfo.data();

    std::vector<VkDescriptorBufferInfo> drawInfo {
            { m_model->draw.u8.handle, 0, VK_WHOLE_SIZE },
            { m_model->draw.u16.handle, 0, VK_WHOLE_SIZE },
            { m_model->draw.u32.handle, 0, VK_WHOLE_SIZE },
    };

    writes[4].dstSet = m_model->rtxDescriptorSet;
    writes[4].dstBinding = 3;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 3;
    writes[4].pBufferInfo = drawInfo.data();

    device().updateDescriptorSets(writes);
}



void gltf::bvh::Bvh::createDescriptorSetLayout(VulkanDevice &device) {
    rtxDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1) // vertices
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(2) // indexes
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(3)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(3) // meshes
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(3)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(4) // draw instance
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(3)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
}

VulkanDescriptorSetLayout  gltf::bvh::Bvh::rtxDescriptorSetLayout;