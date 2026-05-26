#pragma once

// Project includes
#include <cstdint>
#include <glm/glm.hpp>

struct Plane {
    glm::vec3 normal;
    float d;
};

// Global constant buffer
struct GlobalCB {
    glm::mat4 ViewProjectionMatrix{1};
    glm::mat4 InvViewProjectionMatrix{1};
    glm::vec4 ScreenSize{};
    glm::vec3 CameraPosition{};
    glm::vec3 SunDirection{1};
    glm::vec3 WireFrameColor{};
    uint32_t FrameIndex;
    float Time;
    float CullFlag;
    float FoV;
    float WireFrameSize;
    float ScreenSpaceShadow;
    float FarPlaneDistance;
};

struct UpdateCB {
    glm::mat4 UpdateViewProjectionMatrix{1};
    glm::mat4 UpdateInvViewProjectionMatrix{1};
    glm::vec4 FrustumPlanes[6];
    glm::vec3 UpdateCameraPosition{};
    glm::vec3 UpdateCameraForward{};
    float TriangleSize{};
    uint32_t MaxSubdivisionDepth{};
    float UpdateFOV{};
    float UpdateFarPlaneDistance{};
};

struct GeometryCB {
    uint32_t TotalNumElements{};
    uint32_t BaseDepth{};
    uint32_t TotalNumVertices{};
    uint32_t MaterialID{};
};

struct PlanetCB {
    glm::vec3 center{};
    float radius{};
};

struct DeformationCB {
    glm::vec4 PatchSize;
    glm::vec4 PatchRoughness;
    float Choppiness;
    int Attenuation;
    float Amplification;
    uint32_t PatchFlags;
};

struct WaterSimulationCB {
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
