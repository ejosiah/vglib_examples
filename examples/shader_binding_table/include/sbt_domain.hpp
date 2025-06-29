#pragma once

#include "shader_binding_table.hpp"

using AsGeom = VkAccelerationStructureGeometryKHR;
using AsBuildInfo = VkAccelerationStructureBuildRangeInfoKHR;
struct AsGeometryInfo {
    AsGeom geom;
    AsBuildInfo buildInfo;
};

struct AccelerationStructure {
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    VkDeviceAddress deviceAddress = 0;
    VulkanBuffer buffer;
    VkBuildAccelerationStructureFlagsKHR flags = 0;
};

using Blas = AccelerationStructure;
using Tlas = AccelerationStructure;

using GeomItr = std::vector<AsGeometryInfo>::iterator;

struct ScratchBuffer {
    VulkanBuffer handle;
    VkDeviceAddress address = 0;
};

struct InstanceDesc {
    int offset{0};
    int geometryCount{1};
};