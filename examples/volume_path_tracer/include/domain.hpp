#pragma once

#include "common.h"
#include <glm/glm.hpp>

#define DIFFUSE_BRDF_LAMBERTIAN (1 << 0)
#define DIFFUSE_BRDF_OREN_NAYAR (1 << 1)
#define DIFFUSE_BRDF_DISNEY (1 << 2)

#define SPECULAR_BRDF_MICROFACET (1 << 3)
#define SPECLUAR_BRDF_PHONG (1 << 4)

#define BTDF_TRANSMISSION (1 << 5)

#define BRDF_DIFFUSE (DIFFUSE_BRDF_LAMBERTIAN | DIFFUSE_BRDF_OREN_NAYAR | DIFFUSE_BRDF_DISNEY)

#define BRDF_SPECULAR (SPECULAR_BRDF_MICROFACET | SPECLUAR_BRDF_PHONG)

enum class MediumType : int { Homogeneous, Heterogeneous };

struct UniformData {
    glm::mat4 viewInverse;
    glm::mat4 projInverse;
    uint32_t currentSample{};
    uint32_t sampleCount{10000};
    uint32_t maxBounce{50};
    uint32_t frame{};
    uint32_t environment_id{~0u};
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
    float g{0};
    int type{to<int>(MediumType::Homogeneous)};
};