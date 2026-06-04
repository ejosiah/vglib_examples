#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

// Camera properties
constexpr float g_CameraFOV = 40.0f;

// Earth data
constexpr float g_EarthRadius = 6371000.0;
constexpr glm::dvec3 g_EarthCenter = { 0.0, 0.0, 0.0 };
constexpr float g_EarthImpostorToggle = g_EarthRadius * 1.1f;
constexpr float g_EarthTriangleSize = 60.0f;

// Moon data
constexpr float g_MoonRadius = 1737400.0;
constexpr glm::dvec3 g_MoonCenter = glm::dvec3({ 30000000.0, 12000000.0, 0.0 });
constexpr float g_MoonImpostorToggle = g_MoonRadius * 2.0f;
constexpr float g_MoonTriangleSize = 60.0f;

// Convert kmh to mpsec
constexpr float g_KMPerHourToMPerSec = 1.0f / 3.6f;

// Water simulation
constexpr uint32_t g_WaterSimResolution = 256;
constexpr uint32_t g_WaterSimBandCount = 4;
constexpr uint32_t g_WaterSimSurfaceGradientMipCount = 5;
constexpr float pi = glm::pi<float>();
constexpr float half_pi = glm::half_pi<float>();
constexpr glm::vec4 g_WaterSimPatchSize = glm::vec4({ 5.0f * 216.0f * pi * pi * pi, 5 * pi * pi * 36.0f, 5.0f * pi * 6, 5.0f });
constexpr glm::vec4 g_WaterSimPatchRoughness = glm::vec4({ 0.08, 0.04, 0.02, 0.002 });

// Set of materials
#define UNUSED_MATERIAL 0
#define EARTH_MATERIAL 1
#define MOON_MATERIAL 2

// Other constants
#define LEB_MATRIX_CACHE_SIZE 5
