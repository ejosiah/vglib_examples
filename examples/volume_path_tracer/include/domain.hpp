#pragma once

#include "common.h"
#include "Texture.h"
#include <glm/glm.hpp>

#define DIFFUSE_BRDF_LAMBERTIAN (1 << 0)
#define DIFFUSE_BRDF_OREN_NAYAR (1 << 1)
#define DIFFUSE_BRDF_DISNEY (1 << 2)

#define SPECULAR_BRDF_MICROFACET (1 << 3)
#define SPECLUAR_BRDF_PHONG (1 << 4)

#define BTDF_TRANSMISSION (1 << 5)

#define BRDF_DIFFUSE (DIFFUSE_BRDF_LAMBERTIAN | DIFFUSE_BRDF_OREN_NAYAR | DIFFUSE_BRDF_DISNEY)

#define BRDF_SPECULAR (SPECULAR_BRDF_MICROFACET | SPECLUAR_BRDF_PHONG)

enum class Shaders : int {
    RayGen, EnvGen,
    Miss, VolumeMiss,
    Hit, VolumeHit,
    Count };

enum class Rays : int { Primary, Volume, Count };

enum class MediumType : int { Homogeneous, Heterogeneous };

struct UniformData {
    glm::mat4 viewInverse;
    glm::mat4 projInverse;
    uint32_t currentSample{};
    uint32_t sampleCount{10000};
    uint32_t maxBounce{8};
    uint32_t frame{};
    uint32_t RayCount{to<uint32_t>(Rays::Count) };
    uint32_t environment_id{~0u};
    uint32_t environment_light{~0u};
    float worldRadius{20};
};

struct SurfaceInfo {
    int materialId{-1};
    int mediumId{-1};
};

struct MaterialInfo {
    glm::vec3 diffuse{0.6};
    float metalness{0};
    float roughness{0.5};
    int bsdf{DIFFUSE_BRDF_LAMBERTIAN | SPECULAR_BRDF_MICROFACET };
};

struct MediumInfo {
    glm::vec3 scattering{0};
    glm::vec3 absorption{0};
    glm::vec3 extinction{0};
    uint32_t volume{~0u};
    float g{0};
    int type{to<int>(MediumType::Homogeneous)};
};

struct Volume {
    Texture texture;
    glm::mat4 worldToLocal;
    glm::mat4 localToWorld;
    struct {
        glm::vec3 min{MAX_FLOAT};
        glm::vec3 max{MIN_FLOAT};
    } bounds;
    uint32_t binding_id{~0u};
    float maxDensity{0};
};

struct VolumeInstance {
    glm::mat4 textureToWorldSpace{1};
    uint32_t data{~0u};
    float maxDensity{0};
};