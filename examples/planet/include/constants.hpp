#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

// Camera properties
const float g_CameraFOV = glm::radians(20.0f);

// Earth data
const float g_EarthRadius = 6371000.0;
const glm::dvec3 g_EarthCenter = { 0.0, 0.0, 0.0 };
const float g_EarthImpostorToggle = g_EarthRadius * 1.1f;
const float g_EarthTriangleSize = 60.0f;

// Moon data
const float g_MoonRadius = 1737400.0;
const glm::dvec3 g_MoonCenter = glm::dvec3({ 30000000.0, 12000000.0, 0.0 });
const float g_MoonImpostorToggle = g_MoonRadius * 2.0f;
const float g_MoonTriangleSize = 60.0f;

// Convert kmh to mpsec
const float g_KMPerHourToMPerSec = 1.0f / 3.6f;

// Water simulation
const uint32_t g_WaterSimResolution = 256;
const uint32_t g_WaterSimBandCount = 4;
const float pi = glm::pi<float>();
const glm::vec4 g_WaterSimPatchSize = glm::vec4({ 5.0f * 216.0f * pi * pi * pi, 5 * pi * pi * 36.0f, 5.0f * pi * 6, 5.0f });
const glm::vec4 g_WaterSimPatchRoughness = glm::vec4({ 0.08, 0.04, 0.02, 0.002 });

// Set of materials
#define UNUSED_MATERIAL 0
#define EARTH_MATERIAL 1
#define MOON_MATERIAL 2

// Other constants
#define LEB_MATRIX_CACHE_SIZE 5