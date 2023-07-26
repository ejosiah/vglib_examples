#include <cuda.h>
#include <cuda_runtime.h>

#include <optix.h>
#include <optix_function_table.h>
#include <optix_function_table_definition.h>
#include <optix_stubs.h>

#include <spdlog/spdlog.h>
#include <glm/glm.hpp>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <glm\glm.hpp>
#include <glm\gtc\packing.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include "glm_format.h"
#include "vulkan_context.hpp"
#include <array>
#include "spectrum/spectrum.hpp"
#include "openexr_eval.h"
#include "FirstPersonCamera.h"
#include "primitives.h"
#include "halfedge.hpp"
#include <process.h>
#include <meshoptimizer.h>
#include "Mesh.h"
#include "dds.hpp"
#include "stb_image.h"
#include <openvdb/openvdb.h>
#include <taskflow/taskflow.hpp>
#include "fft.hpp"
#include "dft.hpp"
#ifndef STBI_MSC_SECURE_CRT
#define STBI_MSC_SECURE_CRT
#ifndef STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#endif // STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#endif // STBI_MSC_SECURE_CRT

void vdbEval(){
    std::filesystem::current_path(R"(C:\Users\Josiah Ebhomenye\OneDrive\media\volumes\VDB-Clouds-Pack-Pixel-Lab\VDB Cloud Files)");
    openvdb::initialize();

    openvdb::io::File file("cloud_v001_0.02.vdb");
    file.open();

    openvdb::GridBase::Ptr grid;
    std::cout << "grids:\n";
    for(auto nameIter = file.beginName(); nameIter != file.endName(); ++nameIter){
        std::cout << "\tgrid: " << nameIter.gridName() << "\n";
    }

    grid = file.readGrid(file.beginName().gridName());

    std::cout << "\n\nMetadata:\n";
    for(auto metaItr = grid->beginMeta(); metaItr != grid->endMeta(); metaItr++){
        std::cout << "\tmetadata: [" << metaItr->first << ", " << metaItr->second->str() <<  "]" << ", type: " << metaItr->second->typeName() << "\n";
    }


    auto fGrid = openvdb::gridPtrCast<openvdb::FloatGrid>(grid);
    std::cout << "\nbackground: " << fGrid->background() << "\n";

    auto accessor = fGrid->getAccessor();
    openvdb::Coord xyz(8, 38, 20);
    std::cout << "value at center: " << accessor.getValue(xyz) << "\n";

    auto boxMin = fGrid->getMetadata<openvdb::Vec3IMetadata>("file_bbox_min")->value();
    auto boxMax = fGrid->getMetadata<openvdb::Vec3IMetadata>("file_bbox_max")->value();
    decltype(boxMin) center{};
    center = center.add(boxMin, boxMax);
    center = center.div(2, center);

    std::cout << "min bounds: " << boxMin  << "\n";
    std::cout << "center:" << center << "\n";
    std::cout << "max bounds:" << boxMax << "\n";

//    std::cout << "\n\nvalues in grid";
//    for(auto iter = fGrid->cbeginValueOn(); iter; ++iter){
//        std::cout << "Grid" << iter.getCoord() << " = " << *iter << "\n";
//    }

    file.close();
}

void taskFlowEval(){
    tf::Executor executor;
    tf::Taskflow flow;
    tf::Future<void> future;

    auto [A, B, C] = flow.emplace(
        []{ spdlog::info("task A executes"); },
        [&]{  spdlog::info("cancelling flow"); future.cancel(); },
        []{ spdlog::info("task A executes"); }
    );

    A.precede(B, C);
    C.succeed(A, B);
    future = executor.run(flow);

    try {
        future.wait();
    }catch (std::string& err){
        spdlog::error("{}", err);
    }
}

glm::vec3 rRotate(float angle, glm::vec3 v, glm::vec3 axis){
    return v * glm::cos(angle) + glm::cross(axis, v) * glm::sin(angle)
            + axis * dot(axis, v) * (1 - glm::cos(angle));
}


void rect(){
  glm::vec3 min{-0.132965505, 0.754987597, -0.127133697};
  glm::vec3 max{0.139963686, 0.754987597, 0.0933091267};

  auto d = max - min;
  auto dn = glm::normalize(d);
  auto n = glm::vec3(0, -1, 0);
  auto qx = glm::angleAxis(glm::quarter_pi<float>(), n);
  auto x = qx * dn;
  auto qy = glm::angleAxis(-glm::quarter_pi<float>(), n);
  auto y = qy * dn;


  auto p0 = min;
  auto p1 = p0 + x;
  auto p2 = p0 + y;
  auto p3 = max;
  fmt::print("d: {}\n", d);
  fmt::print("p0 : {}\n", p0);
  fmt::print("p1 : {}\n", p1);
  fmt::print("p2 : {}\n", p2);
  fmt::print("p3 : {}\n", p3);


}

#define toKelvin(celsius) (celsius)

static constexpr float MIN_TEMP = toKelvin(-20);  // celsius
static constexpr float AMBIENT_TEMP = toKelvin(0); // celsius
static constexpr float MAX_TEMP = toKelvin(100); // celsius
static constexpr float TARGET_TEMP = toKelvin(150); // celsius
static constexpr float TIME_STEP = 0.008333333333; // seconds

void externalMemory(){
    ContextCreateInfo info{};
    info.instanceExtAndLayers.extensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
//    info.instanceExtAndLayers.extensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
    info.instanceExtAndLayers.extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);
    info.deviceExtAndLayers.extensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
    info.deviceExtAndLayers.extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);

    VulkanContext ctx{info};
    ctx.init();
}

void printRaytracingProps(){
    ContextCreateInfo info{};
    info.deviceExtAndLayers.extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    info.deviceExtAndLayers.extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    info.deviceExtAndLayers.extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    VulkanContext ctx{info};
    ctx.init();
//    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
//    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
//    rtProperties.pNext = &asProperties;
//    VkPhysicalDeviceProperties2 props{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
//    props.pNext = &rtProperties;
//
//    vkGetPhysicalDeviceProperties2(ctx.device, &props);
//
//    fmt::print("VK Ray tracing properties:\n");
//    fmt::print("\tshaderGroupHandleSize: {} bytes\n", rtProperties.shaderGroupHandleSize);
//    fmt::print("\tmaxRayRecursionDepth: {}\n", rtProperties.maxRayRecursionDepth);
//    fmt::print("\tmaxShaderGroupStride: {} bytes\n", rtProperties.maxShaderGroupStride);
//    fmt::print("\tshaderGroupBaseAlignment: {} bytes\n", rtProperties.shaderGroupBaseAlignment);
//    fmt::print("\tshaderGroupHandleCaptureReplaySize: {} bytes\n", rtProperties.shaderGroupHandleCaptureReplaySize);
//    fmt::print("\tmaxRayDispatchInvocationCount: {}\n", rtProperties.maxRayDispatchInvocationCount);
//    fmt::print("\tshaderGroupHandleAlignment: {} bytes\n", rtProperties.shaderGroupHandleAlignment);
//    fmt::print("\tmaxRayHitAttributeSize: {} bytes\n", rtProperties.maxRayHitAttributeSize);
//
//    fmt::print("\n\nAcceleration Structure Properties:\n");
//    fmt::print("\tmaxGeometryCount: {}\n", asProperties.maxGeometryCount);
//    fmt::print("\tmaxInstanceCount: {}\n", asProperties.maxInstanceCount);
//    fmt::print("\tmaxPrimitiveCount: {}\n", asProperties.maxPrimitiveCount);
//    fmt::print("\tmaxPerStageDescriptorAccelerationStructures: {}\n", asProperties.maxPerStageDescriptorAccelerationStructures);
//    fmt::print("\tmaxPerStageDescriptorUpdateAfterBindAccelerationStructures: {}\n", asProperties.maxPerStageDescriptorUpdateAfterBindAccelerationStructures);
//    fmt::print("\tmaxDescriptorSetAccelerationStructures: {}\n", asProperties.maxDescriptorSetAccelerationStructures);
//    fmt::print("\tmaxDescriptorSetUpdateAfterBindAccelerationStructures: {}\n", asProperties.maxDescriptorSetUpdateAfterBindAccelerationStructures);
//    fmt::print("\tminAccelerationStructureScratchOffsetAlignment: {} bytes\n", asProperties.minAccelerationStructureScratchOffsetAlignment);

    VkPhysicalDeviceFeatures features;
    VkPhysicalDeviceFeatures2 features2{};
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynamicSF{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
    features2.pNext = &extDynamicSF;
    vkGetPhysicalDeviceFeatures2(ctx.device, & features2);
    fmt::print("extended features: {:b}\n", extDynamicSF.extendedDynamicState);
}

void montiCarlo(){
    auto a = 0.0;
    auto b = glm::pi<double>();
    auto x = rngFunc(a, b);
    auto f = [](auto x){ return 2.0 * sin(x) * sin(x); };
    auto pdf = [](auto x){ return glm::one_over_pi<float>(); };

    auto integrate = [](auto a, auto b, auto f, auto x, auto N){
        decltype(a) sum = decltype(a)();
        for(int i = 0; i < N; i++){
            sum += f(x());
        }
        return (b - a) * sum / N;
    };

    auto expectedValue = integrate(a, b, f, x, 1 << 20);
    fmt::print("E[x] = {}\n", expectedValue);

    auto integrate1 = [](auto a, auto b, auto f, auto pdf, auto X, auto N){
        decltype(a) sum = decltype(a)();
        for(auto i = 0; i < N; i++){
            auto x = X();
            sum += f(x)/pdf(x);
        }
        return sum / N;
    };

    expectedValue = integrate1(a, b, f, pdf, x, 1 << 20);
    fmt::print("E[x] = {}\n", expectedValue);
}

using namespace glm;

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

vec2 hammersley(uint i, uint N)
{
    return vec2(float(i)/float(N), RadicalInverse_VdC(i));
}

constexpr int numBins = 72;

const float colorMatchingFunc[3][72] =
        {
                {0.001368, 0.002236, 0.004243, 0.007650, 0.014310, 0.023190, 0.043510, 0.077630, 0.134380, 0.214770, 0.283900, 0.328500, 0.348280, 0.348060, 0.336200, 0.318700, 0.290800, 0.251100, 0.195360, 0.142100, 0.095640, 0.057950, 0.032010, 0.014700, 0.004900, 0.002400, 0.009300, 0.029100, 0.063270, 0.109600, 0.165500, 0.225750, 0.290400, 0.359700, 0.433450, 0.512050, 0.594500, 0.678400, 0.762100, 0.842500, 0.916300, 0.978600, 1.026300, 1.056700, 1.062200, 1.045600, 1.002600, 0.938400, 0.854450, 0.751400, 0.642400, 0.541900, 0.447900, 0.360800, 0.283500, 0.218700, 0.164900, 0.121200, 0.087400, 0.063600, 0.046770, 0.032900, 0.022700, 0.015840, 0.011359, 0.008111, 0.005790, 0.004106, 0.002899, 0.002049, 0.001440, 0.000000},
                {0.000039, 0.000064, 0.000120, 0.000217, 0.000396, 0.000640, 0.001210, 0.002180, 0.004000, 0.007300, 0.011600, 0.016840, 0.023000, 0.029800, 0.038000, 0.048000, 0.060000, 0.073900, 0.090980, 0.112600, 0.139020, 0.169300, 0.208020, 0.258600, 0.323000, 0.407300, 0.503000, 0.608200, 0.710000, 0.793200, 0.862000, 0.914850, 0.954000, 0.980300, 0.994950, 1.000000, 0.995000, 0.978600, 0.952000, 0.915400, 0.870000, 0.816300, 0.757000, 0.694900, 0.631000, 0.566800, 0.503000, 0.441200, 0.381000, 0.321000, 0.265000, 0.217000, 0.175000, 0.138200, 0.107000, 0.081600, 0.061000, 0.044580, 0.032000, 0.023200, 0.017000, 0.011920, 0.008210, 0.005723, 0.004102, 0.002929, 0.002091, 0.001484, 0.001047, 0.000740, 0.000520, 0.000000},
                {0.006450, 0.010550, 0.020050, 0.036210, 0.067850, 0.110200, 0.207400, 0.371300, 0.645600, 1.039050, 1.385600, 1.622960, 1.747060, 1.782600, 1.772110, 1.744100, 1.669200, 1.528100, 1.287640, 1.041900, 0.812950, 0.616200, 0.465180, 0.353300, 0.272000, 0.212300, 0.158200, 0.111700, 0.078250, 0.057250, 0.042160, 0.029840, 0.020300, 0.013400, 0.008750, 0.005750, 0.003900, 0.002750, 0.002100, 0.001800, 0.001650, 0.001400, 0.001100, 0.001000, 0.000800, 0.000600, 0.000340, 0.000240, 0.000190, 0.000100, 0.000050, 0.000030, 0.000020, 0.000010, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000, 0.000000},
        };

const float spectralData[24][36] = {
        {0.055, 0.058, 0.061, 0.062, 0.062, 0.062, 0.062, 0.062, 0.062, 0.062, 0.062, 0.063, 0.065, 0.070, 0.076, 0.079, 0.081, 0.084, 0.091, 0.103, 0.119, 0.134, 0.143, 0.147, 0.151, 0.158, 0.168, 0.179, 0.188, 0.190, 0.186, 0.181, 0.182, 0.187, 0.196, 0.209},
        {0.117, 0.143, 0.175, 0.191, 0.196, 0.199, 0.204, 0.213, 0.228, 0.251, 0.280, 0.309, 0.329, 0.333, 0.315, 0.286, 0.273, 0.276, 0.277, 0.289, 0.339, 0.420, 0.488, 0.525, 0.546, 0.562, 0.578, 0.595, 0.612, 0.625, 0.638, 0.656, 0.678, 0.700, 0.717, 0.734},
        {0.130, 0.177, 0.251, 0.306, 0.324, 0.330, 0.333, 0.331, 0.323, 0.311, 0.298, 0.285, 0.269, 0.250, 0.231, 0.214, 0.199, 0.185, 0.169, 0.157, 0.149, 0.145, 0.142, 0.141, 0.141, 0.141, 0.143, 0.147, 0.152, 0.154, 0.150, 0.144, 0.136, 0.132, 0.135, 0.147},
        {0.051, 0.054, 0.056, 0.057, 0.058, 0.059, 0.060, 0.061, 0.062, 0.063, 0.065, 0.067, 0.075, 0.101, 0.145, 0.178, 0.184, 0.170, 0.149, 0.133, 0.122, 0.115, 0.109, 0.105, 0.104, 0.106, 0.109, 0.112, 0.114, 0.114, 0.112, 0.112, 0.115, 0.120, 0.125, 0.130},
        {0.144, 0.198, 0.294, 0.375, 0.408, 0.421, 0.426, 0.426, 0.419, 0.403, 0.379, 0.346, 0.311, 0.281, 0.254, 0.229, 0.214, 0.208, 0.202, 0.194, 0.193, 0.200, 0.214, 0.230, 0.241, 0.254, 0.279, 0.313, 0.348, 0.366, 0.366, 0.359, 0.358, 0.365, 0.377, 0.398},
        {0.136, 0.179, 0.247, 0.297, 0.320, 0.337, 0.355, 0.381, 0.419, 0.466, 0.510, 0.546, 0.567, 0.574, 0.569, 0.551, 0.524, 0.488, 0.445, 0.400, 0.350, 0.299, 0.252, 0.221, 0.204, 0.196, 0.191, 0.188, 0.191, 0.199, 0.212, 0.223, 0.232, 0.233, 0.229, 0.229},
        {0.054, 0.054, 0.053, 0.054, 0.054, 0.055, 0.055, 0.055, 0.056, 0.057, 0.058, 0.061, 0.068, 0.089, 0.125, 0.154, 0.174, 0.199, 0.248, 0.335, 0.444, 0.538, 0.587, 0.595, 0.591, 0.587, 0.584, 0.584, 0.590, 0.603, 0.620, 0.639, 0.655, 0.663, 0.663, 0.667},
        {0.122, 0.164, 0.229, 0.286, 0.327, 0.361, 0.388, 0.400, 0.392, 0.362, 0.316, 0.260, 0.209, 0.168, 0.138, 0.117, 0.104, 0.096, 0.090, 0.086, 0.084, 0.084, 0.084, 0.084, 0.084, 0.085, 0.090, 0.098, 0.109, 0.123, 0.143, 0.169, 0.205, 0.244, 0.287, 0.332},
        {0.096, 0.115, 0.131, 0.135, 0.133, 0.132, 0.130, 0.128, 0.125, 0.120, 0.115, 0.110, 0.105, 0.100, 0.095, 0.093, 0.092, 0.093, 0.096, 0.108, 0.156, 0.265, 0.399, 0.500, 0.556, 0.579, 0.588, 0.591, 0.593, 0.594, 0.598, 0.602, 0.607, 0.609, 0.609, 0.610},
        {0.092, 0.116, 0.146, 0.169, 0.178, 0.173, 0.158, 0.139, 0.119, 0.101, 0.087, 0.075, 0.066, 0.060, 0.056, 0.053, 0.051, 0.051, 0.052, 0.052, 0.051, 0.052, 0.058, 0.073, 0.096, 0.119, 0.141, 0.166, 0.194, 0.227, 0.265, 0.309, 0.355, 0.396, 0.436, 0.478},
        {0.061, 0.061, 0.062, 0.063, 0.064, 0.066, 0.069, 0.075, 0.085, 0.105, 0.139, 0.192, 0.271, 0.376, 0.476, 0.531, 0.549, 0.546, 0.528, 0.504, 0.471, 0.428, 0.381, 0.347, 0.327, 0.318, 0.312, 0.310, 0.314, 0.327, 0.345, 0.363, 0.376, 0.381, 0.378, 0.379},
        {0.063, 0.063, 0.063, 0.064, 0.064, 0.064, 0.065, 0.066, 0.067, 0.068, 0.071, 0.076, 0.087, 0.125, 0.206, 0.305, 0.383, 0.431, 0.469, 0.518, 0.568, 0.607, 0.628, 0.637, 0.640, 0.642, 0.645, 0.648, 0.651, 0.653, 0.657, 0.664, 0.673, 0.680, 0.684, 0.688},
        {0.066, 0.079, 0.102, 0.146, 0.200, 0.244, 0.282, 0.309, 0.308, 0.278, 0.231, 0.178, 0.130, 0.094, 0.070, 0.054, 0.046, 0.042, 0.039, 0.038, 0.038, 0.038, 0.038, 0.039, 0.039, 0.040, 0.041, 0.042, 0.044, 0.045, 0.046, 0.046, 0.048, 0.052, 0.057, 0.065},
        {0.052, 0.053, 0.054, 0.055, 0.057, 0.059, 0.061, 0.066, 0.075, 0.093, 0.125, 0.178, 0.246, 0.307, 0.337, 0.334, 0.317, 0.293, 0.262, 0.230, 0.198, 0.165, 0.135, 0.115, 0.104, 0.098, 0.094, 0.092, 0.093, 0.097, 0.102, 0.108, 0.113, 0.115, 0.114, 0.114},
        {0.050, 0.049, 0.048, 0.047, 0.047, 0.047, 0.047, 0.047, 0.046, 0.045, 0.044, 0.044, 0.045, 0.046, 0.047, 0.048, 0.049, 0.050, 0.054, 0.060, 0.072, 0.104, 0.178, 0.312, 0.467, 0.581, 0.644, 0.675, 0.690, 0.698, 0.706, 0.715, 0.724, 0.730, 0.734, 0.738},
        {0.058, 0.054, 0.052, 0.052, 0.053, 0.054, 0.056, 0.059, 0.067, 0.081, 0.107, 0.152, 0.225, 0.336, 0.462, 0.559, 0.616, 0.650, 0.672, 0.694, 0.710, 0.723, 0.731, 0.739, 0.746, 0.752, 0.758, 0.764, 0.769, 0.771, 0.776, 0.782, 0.790, 0.796, 0.799, 0.804},
        {0.145, 0.195, 0.283, 0.346, 0.362, 0.354, 0.334, 0.306, 0.276, 0.248, 0.218, 0.190, 0.168, 0.149, 0.127, 0.107, 0.100, 0.102, 0.104, 0.109, 0.137, 0.200, 0.290, 0.400, 0.516, 0.615, 0.687, 0.732, 0.760, 0.774, 0.783, 0.793, 0.803, 0.812, 0.817, 0.825},
        {0.108, 0.141, 0.192, 0.236, 0.261, 0.286, 0.317, 0.353, 0.390, 0.426, 0.446, 0.444, 0.423, 0.385, 0.337, 0.283, 0.231, 0.185, 0.146, 0.118, 0.101, 0.090, 0.082, 0.076, 0.074, 0.073, 0.073, 0.074, 0.076, 0.077, 0.076, 0.075, 0.073, 0.072, 0.074, 0.079},
        {0.189, 0.255, 0.423, 0.660, 0.811, 0.862, 0.877, 0.884, 0.891, 0.896, 0.899, 0.904, 0.907, 0.909, 0.911, 0.910, 0.911, 0.914, 0.913, 0.916, 0.915, 0.916, 0.914, 0.915, 0.918, 0.919, 0.921, 0.923, 0.924, 0.922, 0.922, 0.925, 0.927, 0.930, 0.930, 0.933},
        {0.171, 0.232, 0.365, 0.507, 0.567, 0.583, 0.588, 0.590, 0.591, 0.590, 0.588, 0.588, 0.589, 0.589, 0.591, 0.590, 0.590, 0.590, 0.589, 0.591, 0.590, 0.590, 0.587, 0.585, 0.583, 0.580, 0.578, 0.576, 0.574, 0.572, 0.571, 0.569, 0.568, 0.568, 0.566, 0.566},
        {0.144, 0.192, 0.272, 0.331, 0.350, 0.357, 0.361, 0.363, 0.363, 0.361, 0.359, 0.358, 0.358, 0.359, 0.360, 0.360, 0.361, 0.361, 0.360, 0.362, 0.362, 0.361, 0.359, 0.358, 0.355, 0.352, 0.350, 0.348, 0.345, 0.343, 0.340, 0.338, 0.335, 0.334, 0.332, 0.331},
        {0.105, 0.131, 0.163, 0.180, 0.186, 0.190, 0.193, 0.194, 0.194, 0.192, 0.191, 0.191, 0.191, 0.192, 0.192, 0.192, 0.192, 0.192, 0.192, 0.193, 0.192, 0.192, 0.191, 0.189, 0.188, 0.186, 0.184, 0.182, 0.181, 0.179, 0.178, 0.176, 0.174, 0.173, 0.172, 0.171},
        {0.068, 0.077, 0.084, 0.087, 0.089, 0.090, 0.092, 0.092, 0.091, 0.090, 0.090, 0.090, 0.090, 0.090, 0.090, 0.090, 0.090, 0.090, 0.090, 0.090, 0.090, 0.089, 0.089, 0.088, 0.087, 0.086, 0.086, 0.085, 0.084, 0.084, 0.083, 0.083, 0.082, 0.081, 0.081, 0.081},
        {0.031, 0.032, 0.032, 0.033, 0.033, 0.033, 0.033, 0.033, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.032, 0.033}
};

void spectrumToXYZ(int colorIndex, vec3& XYZ)
{
    float S = 0;
    for (int i = 0; i < 36; ++i) {
        XYZ.x += colorMatchingFunc[0][i * 2] * spectralData[colorIndex][i];
        XYZ.y += colorMatchingFunc[1][i * 2] * spectralData[colorIndex][i];
        XYZ.z += colorMatchingFunc[2][i * 2] * spectralData[colorIndex][i];
        S += colorMatchingFunc[1][i * 2];
    }
    XYZ.x /= S;
    XYZ.y /= S;
    XYZ.z /= S;
}

const mat3 XYZ_TO_RGB = mat3(
        2.3706743, -0.5138850, 0.0052982,
        -0.9000405, 1.4253036, -0.0146949,
        -0.4706338, 0.0885814, 1.0093968
);

const float gamma = 2.2;

//vec3 getColor(ivec2 id){
//    int i = id.y * 6 + id.x;
//    static std::map<int, vec3> cache;
//    if(cache.find(i) != cache.end()){
//        return cache[i];
//    }
//    vec3 xyz{0};
//    spectrumToXYZ(i, xyz);
//
//    vec3 rgb =  max(vec3(0), XYZ_TO_RGB * xyz);
//    rgb = pow(rgb, vec3(1/gamma));
//
//    cache[i] = rgb;
//
//    return rgb;
//}

void macbethChart(){

    auto getColorChecker = []{
        std::string spdPath = "../../data/spd";
        std::array<glm::vec3, 24> colorChecker{};
        for(int i = 1; i <= 24; i++){
            auto filename = fmt::format("{}/macbeth-{}.spd", spdPath, i);
            auto spd = spectrum::loadSpd(filename);
            auto sampled = spectrum::Sampled<>::fromSampled(spd);
            auto rgb = sampled.toRGB();
            rgb = pow(rgb, vec3(1/gamma));
            colorChecker[i - 1] = rgb;
        }
        return colorChecker;
    };

    const auto colorChecker = getColorChecker();
    ivec2 res{1200, 800};
    std::ofstream fout{R"(C:\Users\Josiah Ebhomenye\Pictures\macbeth_chart.ppm)"};
    if(fout.bad()) throw std::runtime_error{"unable to create macbeth file"};
    auto header = fmt::format("P3\n{} {}\n{}\n", res.x, res.y, 255);
    fout << header;
    for(int i = 0; i < res.y; i++){
        for(int j = 0; j < res.x; j++){
            vec2 uv{static_cast<float>(j) / res.x, static_cast<float>(i) / res.y };
            ivec2 id{floor(uv * vec2(6, 4))};
//            auto c = getColor(id) * 255.f;
            auto c = colorChecker[id.y * 6 + id.x] * 255.f;
            fout << fmt::format("{} {} {}\n", int(c.x), int(c.y), int(c.z));
        }
    }
    fout.close();
    fmt::print("macbeth_chart.ppm successfully created");
}

int search(std::vector<int> array, int value){
    auto loop = [=](int low, int high){
        while(low <= high){
            int mid = low + (high - low)/2;
            if(value == array[mid]){
                return mid;
            }else if(value > array[mid]){
                low = mid + 1;
            }else {
                high = mid - 1;
            }
        }
        return -(low + 1);
    };

    return loop(0, array.size() - 1);

}

template <typename Predicate>
int FindInterval(int size, const Predicate &pred) {
    int first = 0, len = size;
    while (len > 0) {
        int half = len >> 1, middle = first + half;
        // Bisect range based on value of _pred_ at _middle_
        if (pred(middle)) {
            first = middle + 1;
            len -= half + 1;
        } else
            len = half;
    }
    return glm::clamp(first - 1, 0, size - 2);
}

int search1(std::vector<int> array, int value){
    return FindInterval(array.size(), [array, value](auto index){ return array[index] <= value; });
//    auto findInterval = [](auto size, const auto pred){
//        int first = 0, len = size;
//        while(len > 0){
//            int half = len >> 1;
//            int middle = first + half;
//            if(pred(middle)){
//                first = middle + 1;
//                len -= half + 1;
//            }else {
//                len = half;
//            }
//        }
//        return std::clamp(first - 1, 0, int(size - 2));
//    };
//
//    return findInterval(array.size(), [=, &array](const auto& mid){ return array[mid] <= value; });

}



void searchTest(){

    std::vector<int> array{10, 20, 30, 40, 60, 110, 120, 130, 170};
    auto u = [size=array.size()](int i){
//        return  glm::round( (float(i + .5)/float(size)) * size - .5);
        return i;
    };

    for(auto v : array){
        fmt::print("index: {}, value: {}\n", u(search1(array, v)), v);
    }
    fmt::print("\nindex: {}, value: {}\n", u(search1(array, 35)), 35);
    fmt::print("index: {}, value: {}\n", u(search1(array, 115)), 115);
    fmt::print("index: {}, value: {}\n", u(search1(array, 5)), 5);
}

inline glm::vec2 rootsOfUnity(float n){
    auto f = glm::two_pi<float>() * n;
    return {glm::cos(f), glm::sin(f)};
}

template<typename T>
void save(const std::string& path, const std::vector<T>& data){
    std::ofstream fout(path, std::ios::binary);
    if(!fout.good()){
        fmt::print("error opening output file");
        std::exit(120);
    }
    fout.write(reinterpret_cast<char*>(const_cast<T*>(data.data())), BYTE_SIZE(data));
    fout.flush();
    fout.close();
}

struct Triangle{
    std::array<glm::vec3, 3> v;
};

#define LEFT_PLANE 0
#define RIGHT_PLANE 1
#define BOTTOM_PLANE 2
#define TOP_PLANE 3
#define NEAR_PLANE 4
#define FAR_PLANE 5

struct Frustum{
    vec4 planes[6];
    vec4 corners[8];
};

const vec4 corners[8] = {
        vec4(-1, -1, 0, 1),
        vec4( 1, -1, 0, 1),
        vec4( 1,  1, 0, 1),
        vec4(-1,  1, 0, 1),
        vec4(-1, -1, 1, 1),
        vec4( 1, -1, 1, 1),
        vec4( 1,  1, 1, 1),
        vec4(-1,  1, 1, 1)
};

void getFrustumPlanes(mat4 viewProjection, vec4* planes){

    mat4 vp = transpose(viewProjection);

    planes[LEFT_PLANE]      = vp[3] + vp[0];
    planes[RIGHT_PLANE]     = vp[3] - vp[0];
    planes[BOTTOM_PLANE]    = vp[3] + vp[1];
    planes[TOP_PLANE]       = vp[3] - vp[1];
    planes[NEAR_PLANE]      = vp[3] + vp[2];
    planes[FAR_PLANE]       = vp[3] - vp[2];
}

void getFrustumCorners(mat4 viewProjection, vec4* points){

    mat4 invVP = inverse(viewProjection);

    for(int i = 0; i < 8; i++){
        const vec4 q = invVP * corners[i];
        points[i] = q / q.w;
    }
}

Frustum createFrustum(mat4 viewProjection){
    Frustum frustum{};
    getFrustumPlanes(viewProjection, frustum.planes);
    getFrustumCorners(viewProjection, frustum.corners);

    return frustum;
}


struct Ray{
    vec2 p;
    vec2 q;
};

struct Sphere{
    vec2 center;
    float radius;
};

bool test(const Ray& r, const Sphere& s, float& t0, float& t1){
    auto d = normalize(r.q - r.p);
    auto m = r.p - s.center;
    auto rr = s.radius * s.radius;

    auto b = dot(m, d);
    auto c = dot(m, m) - rr;

    if(c > 0 && b > 0) return false;

    auto discriminant = b*b - c;
    if(discriminant < 0) return false;

    auto sqrtDiscr = glm::sqrt(discriminant);
    t0 = -b - sqrtDiscr;
    t1 = -b + sqrtDiscr;

    return true;
}

bool test2(const Ray& r, const Sphere& s, float& t0, float& t1){
    auto d = normalize(r.q - r.p);
    auto m = r.p - s.center;
    auto rr = s.radius * s.radius;

    float a = dot(d, d);
    float b = -dot(m, d);

    vec2 l = m +( b/a) * d;

    float discr = a * (rr - dot(l, l));

    if(discr < 0.) return false;

    float c = dot(m, m) - rr;

    if(c > 0. && b < 0.) return false;

    float q = b + glm::sign(b) * sqrt(discr);

    t0 = c/q;
    t1 = q/a;

    return true;
}

template<typename Iterator>
void rearrange(Iterator first, Iterator last, int log2N){
    if(log2N <= 1) return;

    auto even = first;
    auto odd = std::partition(first, last, [log2N](auto i){
        int mask = 8 >> log2N;
        return (i & mask) == 0;
    });

    rearrange(even, odd, log2N - 1);
    rearrange(odd, last, log2N - 1);
}

int main(){
//    std::vector<mesh::Mesh> meshes;
//    mesh::load(meshes, R"(C:\Users\Josiah Ebhomenye\OneDrive\media\models\ChineseDragon.obj)");
//    auto sphere = primitives::sphere(100, 100, 1, glm::mat4{1}, glm::vec4(1), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
//    auto cube = primitives::cube();
//    auto prim = meshes.front();
//
//    fmt::print("before optimization:\n");
//    fmt::print("\tnumVertices: {}, numIndices: {}\n", prim.vertices.size(), prim.indices.size());
//
//    std::vector<uint32_t> remap(prim.indices.size());
//    auto vertexCount = meshopt_generateVertexRemap(remap.data(), prim.indices.data(), prim.indices.size(),
//                                                   prim.vertices.data(), prim.vertices.size(), sizeof(Vertex));
//
//    std::vector<uint32_t> remappedIndices(prim.indices.size());
//    meshopt_remapIndexBuffer(remappedIndices.data(), prim.indices.data(), prim.indices.size(), remap.data());
//
//    std::vector<Vertex> remappedVertices(vertexCount);
//    meshopt_remapVertexBuffer(remappedVertices.data(), prim.vertices.data(), prim.vertices.size(), sizeof(Vertex), remap.data());
//
//    std::vector<uint32_t> reOrderedIndices(remappedIndices.size());
//    meshopt_optimizeVertexCache(reOrderedIndices.data(), remappedIndices.data(), remappedIndices.size(), vertexCount);
//
//    fmt::print("\n\nafter optimization:\n");
//    fmt::print("\tnumVertices: {}, numIndices: {}\n", remappedVertices.size(), remappedIndices.size());

//    auto sphere = primitives::sphere(100, 100, 1, glm::mat4{1}, glm::vec4(1), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
//
//    std::vector<glm::vec4> vertices;
//    std::vector<glm::vec4> normals;
//    std::vector<glm::vec4> colors;
//    std::vector<glm::vec2> uvs;
//    for(auto index : sphere.indices){
//        auto& vertex = sphere.vertices[index];
//        vertices.push_back(vertex.position);
//        normals.push_back(glm::vec4(vertex.normal, 0));
//        colors.push_back(vertex.color);
//        uvs.push_back(vertex.uv);
//    }
//    save(R"(D:\Program Files\SHADERed\sphere_vertices.dat)", vertices);
//    save(R"(D:\Program Files\SHADERed\sphere_normals.dat)", normals);
//    save(R"(D:\Program Files\SHADERed\sphere_color.dat)", colors);
//    save(R"(D:\Program Files\SHADERed\sphere_uv.dat)", uvs);

//    auto input = R"(C:\Users\Josiah Ebhomenye\OneDrive\media\textures\Portrait-8.jpg)";
//    int width, height, channels;
//    auto pixels = stbi_load(input, &width, &height, &channels, STBI_rgb_alpha);
//    if(!pixels){
//        spdlog::error("failed to load texture image {}\n", input);
//        return 100;
//    }
//    auto size = width * height * STBI_rgb_alpha;
//    std::vector<char> data(size);
//    std::memcpy(data.data(), pixels, size);
//    stbi_image_free(pixels);
//
//
//    dds::SaveInfo info{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
//
//    info.channelSize = 1;
//    info.numChannels = STBI_rgb_alpha;
//    info.path = R"(D:\Program Files\SHADERed\portrait.dds)";
//
//    dds::save(info, data.data());

   // taskFlowEval();

   int N = 512;
   int NUM_BUTTER_FLIES = static_cast<int>(std::log2(N));

    std::vector<std::complex<double>> butterflyLookup(N * NUM_BUTTER_FLIES);
    std::vector<int> lookupIndex(N * NUM_BUTTER_FLIES * 2);

    createButterflyLookups(lookupIndex, butterflyLookup, NUM_BUTTER_FLIES, true);

    std::vector<vec4> cx_out;
    for(const auto& c : butterflyLookup){
        cx_out.emplace_back(c.real(), c.imag(), 0, 0);
    }
    int width = N;
    int height = NUM_BUTTER_FLIES;
    std::string path = fmt::format(R"(D:\Program Files\SHADERed\butter_fly_lookup_weights_{}.hdr)", N);
    stbi_write_hdr(path.c_str(), width, height, 4, reinterpret_cast<const float*>(cx_out.data()));


    std::vector<vec4> index_out;
    for(int i = 0; i < lookupIndex.size(); i += 2){
        index_out.emplace_back(lookupIndex[i], lookupIndex[i+1], 0, 0);
    }
    path = fmt::format(R"(D:\Program Files\SHADERed\butter_fly_lookup_index_{}.hdr)", N);
    stbi_write_hdr(path.c_str(), width, height, 4, reinterpret_cast<const float*>(index_out.data()));


}
