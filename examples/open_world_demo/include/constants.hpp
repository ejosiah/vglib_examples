#pragma once
#include "common.h"

constexpr float EARTH_RADIUS = 6371 * km;
constexpr float CLOUD_MIN = 1.5 * km;
constexpr float CLOUD_MAX = 5.0 * km;
constexpr float SUN_DISTANCE = 100000 * km;
constexpr float MAX_HEIGHT = 8.849 * km;

constexpr glm::vec3 EARTH_CENTER{0, -EARTH_RADIUS, 0};

constexpr float kSunAngularRadius = 0.00935 / 2;;
constexpr float kSunSolidAngle = glm::pi<float>() * kSunAngularRadius * kSunAngularRadius;
constexpr float kLengthUnitInMeters = 1000;
