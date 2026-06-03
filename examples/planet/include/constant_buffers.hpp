#pragma once

// Project includes
#include <cstdint>
#include <glm/glm.hpp>

#include <array>
#include <type_traits>

#include "VulkanBuffer.h"

#ifndef EnableDoublePrecisionLEB
#define EnableDoublePrecisionLEB 1
#endif

inline constexpr bool UseDoublePrecisionPlanet = EnableDoublePrecisionLEB != 0;
using PlanetScalar = std::conditional_t<UseDoublePrecisionPlanet, double, float>;
using PlanetVec2 = glm::vec<2, PlanetScalar, glm::defaultp>;
using PlanetVec3 = glm::vec<3, PlanetScalar, glm::defaultp>;
using PlanetVec4 = glm::vec<4, PlanetScalar, glm::defaultp>;
using PlanetMat4 = glm::mat<4, 4, PlanetScalar, glm::defaultp>;

template<typename Scalar>
struct PlaneT {
    glm::vec<3, Scalar, glm::defaultp> normal;
    Scalar d;
};

// Global constant buffer
template<typename Scalar>
struct GlobalCBT {
    glm::mat<4, 4, Scalar, glm::defaultp> ViewProjectionMatrix{0};
    glm::mat<4, 4, Scalar, glm::defaultp> InvViewProjectionMatrix{0};
    glm::vec<3, Scalar, glm::defaultp> CameraPosition{};
    glm::vec<3, Scalar, glm::defaultp> SunDirection{1};
    glm::vec<3, Scalar, glm::defaultp> WireFrameColor{};
    glm::vec<2, Scalar, glm::defaultp> ScreenSize{};
    Scalar Time{};
    Scalar CullFlag{};
    Scalar FoV{};
    Scalar WireFrameSize{};
    Scalar ScreenSpaceShadow{};
    Scalar FarPlaneDistance{};
    uint32_t FrameIndex{};
};

template<typename Scalar>
struct UpdateCBT {
    glm::mat<4, 4, Scalar, glm::defaultp> ViewProjectionMatrix{1};
    glm::mat<4, 4, Scalar, glm::defaultp> InvViewProjectionMatrix{1};
    std::array<glm::vec<4, Scalar, glm::defaultp>, 6> FrustumPlanes{};
    glm::vec<3, Scalar, glm::defaultp> CameraPosition{};
    glm::vec<3, Scalar, glm::defaultp> CameraForward{};
    Scalar TriangleSize{};
    Scalar FOV{};
    Scalar FarPlaneDistance{};
    uint32_t MaxSubdivisionDepth{};
};

struct GeometryCB {
    uint32_t TotalNumElements{};
    uint32_t BaseDepth{};
    uint32_t TotalNumVertices{};
    uint32_t MaterialID{};
};

template<typename Scalar>
struct PlanetCBT {
    glm::vec<3, Scalar, glm::defaultp> center{};
    Scalar radius{};
};

using Plane = PlaneT<PlanetScalar>;
using GlobalCB = GlobalCBT<PlanetScalar>;
using UpdateCB = UpdateCBT<PlanetScalar>;
using PlanetCB = PlanetCBT<PlanetScalar>;

struct DeformationData {
    glm::vec4 PatchSize;
    glm::vec4 PatchRoughness;
    float Choppiness;
    int Attenuation;
    float Amplification;
    uint32_t PatchFlags;
};

struct WaterSimulationData {
    glm::vec4 PatchSize;
    glm::vec4 PatchWindOrientation;
    glm::vec4 PatchDirectionDampener;
    glm::vec4 PatchWindSpeed;
    uint32_t SimulationRes;
    float SimulationTime;
    float Choppiness;
    float Amplification;
};

struct MoonCB {
    glm::uvec2 ElevationTextureSize;
    glm::uvec2 DetailTextureSize;
    float PatchSize;
    float PatchAmplitude;
    int32_t NumOctaves;
    uint32_t Attenuation;
};

template<typename T>
struct ConstantBufferT {
    T* cpu{};
    VulkanBuffer gpu;
};

using DeformationCB = ConstantBufferT<DeformationData>;
using WaterSimulationCB = ConstantBufferT<WaterSimulationData>;
