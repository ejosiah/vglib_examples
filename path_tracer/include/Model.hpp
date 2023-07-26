#pragma once

#include "common.h"

enum class Shape : int {
    Rectangle = 0,
    Sphere,
    Disk,
    Polygon
};

struct Rectangle_{
    alignas(16) glm::vec3 p0;
    alignas(16) glm::vec3 p1;
    alignas(16) glm::vec3 p2;
    alignas(16) glm::vec3 p3;
};

struct Sphere{
    glm::vec3 center;
    float radius;
};

struct Disk{
    glm::vec3 center;
    float radius;
    float height;
};

struct Polygon{
    int instanceId{-1};
    int numTriangles{0};
    int triangleOffset{0};
    float area;
};

struct ShapeRef{
    int objectId;
    int shapeId;
    int shape;
    int padding;
};

enum ObjectTypes : uint32_t {
    eNone = 0x0,
    eCornellBox = 0x1,
    eLights = 0x2,
    ePlane = 0x4,
    eDragon = 0x8,
    eVolume = 0x16,
    eAllObjects = eCornellBox | eLights | ePlane | eDragon
};

enum HitGroup : int {
    General = 0,
    Volume = 1,
    Glass = 2,
};

enum HitShaders : int {
    primary = 0,
    volume,
    glass,
    occlusionPrimary,
    occlusionVolume,
    occlusionGlass
};

enum LightFlags : uint32_t {
    None = 0x0,
    DeltaPosition = 0x1,
    DeltaDirection = 0x2,
    Area = 0x4,
    Infinite = 0x8,
    HasPrimitive = 0x16
};

enum class DiffuseBrdf : int {
    None = 0,
    Lambertian = 1,
    OrenNayar = 2,
    Disney = 3
};

enum class SpecularBrdf : int {
    None = 0,
    Microfacet = 1,
    Phong = 2
};

enum class Ndf : int {
    GGX = 1,
    Beckmann = 2
};

struct Light{
    glm::vec3 position{0};
    uint32_t flags{0};

    glm::vec3 normal{0};
    int shapeType{-1};

    glm::vec3 value{0};
    int shapeId{0};

    float cosWidth{1};
    float fallOffStart{0};
    int envMapId;
    int padding;
};

constexpr int SHORT_BOX_MAT_ID = 6;
constexpr int TALL_BOX_MAT_ID = 7;

struct Material{
    alignas(16) glm::vec3 diffuse;
    alignas(16) glm::vec3 ambient;
    alignas(16) glm::vec3 metalness;
    alignas(16) glm::vec3 emission;
    alignas(16) glm::vec3 transmittance;
    float roughness = 0;
    float ior = 0;
    float opacity = 1;
    float illum = 1;
};

struct Medium{
    alignas(16) glm::vec3 absorptionCoeff{0};
    alignas(16) glm::vec3 scatteringCoeff{0};
    float g{0};
};

struct Model {
    static constexpr int SunLightId = 2;

    [[nodiscard]]
    glm::vec3 sunDirection() const {
        return -lights[SunLightId].normal;
    }

    struct {
        uint32_t maxBounces{8};
        uint32_t frame{0};
        uint32_t currentSample{0};
        uint32_t numSamples{10000};
        uint32_t numLights{0};
        int adaptiveSampling{1};
        float worldRadius{0};
        float pMargialIntegral{1};
        uint32_t mask{eLights | eCornellBox | ePlane};
        float exposure{0.8};
        float skyIntensity{0};
        float envMapIntensity{1};
        int planeId{-1};
    } sceneConstants;

    struct {
        int combineBrdfWithFresnel{1};
        int useOptimizedG2{1};
        int useHeightCorrelatedG2{1};
        int specularBrdfType{static_cast<int>(SpecularBrdf::Microfacet)};
        int diffuseBrdfType{static_cast<int>(DiffuseBrdf::Lambertian)};
        int ndfFunction{static_cast<int>(Ndf::GGX)};
        int g2DivideByDenominator{1};
        int shadowRayInRis{0};
    } specializationConstants;

     static constexpr uint32_t MaxSamples{10000000};

    Light* lights;
    int numLights{0};
    float fps;
    Material* cornellMaterials{};
    Material* floorMaterial{};
    Material* dragonMaterial{};
    bool dragonReady{};
    bool directLighting{false};
    float colorTemp{2750};
    struct {
        float azimuth{0};
        float elevation{-10};
        float intensity{10};
        bool enabled{true};
    } sun;

    struct {
        float intensity{10};
        bool enabled{false};
    } headLight;

    std::vector<rt::MeshObjectInstance>* instances;

    bool* invalidateSwapChain;
    bool denoise{false};

    Medium* mediums{};
};