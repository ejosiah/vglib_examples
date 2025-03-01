#include <filesystem>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <fmt/format.h>
#include <glm_format.h>
#include "nishita.hpp"

constexpr auto maxDepth = 20UL;
constexpr auto iniMaxDepth = maxDepth;

const char* gProgName = "";
const float earthRadius = 6360 * km;
namespace fs = std::filesystem;

int main() {
    const auto kFovY = 50.f / 180.f * glm::pi<float>();
    const auto kTanFovY = glm::tan(kFovY / 2);
    const auto aspectRatio = 1;

    const auto view_from_clip = glm::transpose(glm::mat4{
            kTanFovY * aspectRatio, 0, 0, 0,
            0, kTanFovY, 0, 0,
            0, 0, 0, -1,
            0, 0, 1, 1
    });

    float viewZenithAngleRadians = 1.47;
    float viewAzimuthAngleRadians = 0;
    const auto cosZ = glm::cos(viewZenithAngleRadians);
    const auto sinZ = glm::sin(viewZenithAngleRadians);
    const auto cosA = glm::cos(viewAzimuthAngleRadians);
    const auto sinA = glm::sin(viewAzimuthAngleRadians);
    const auto viewDistance = 9000.f;

    float vDist = viewDistance + earthRadius;
    const auto model_from_view = glm::transpose(glm::mat4{
            -sinA, -cosZ * cosA,  sinZ * cosA, sinZ * cosA * vDist,
            0, sinZ, cosZ, cosZ * vDist,
            cosA, -cosZ * sinA, sinZ * sinA, sinZ * sinA * vDist,
            0, 0, 0, 1
    });



    auto camera = glm::column(model_from_view, 3);
    fmt::print("camera: {}\n", camera);

    auto view = model_from_view * glm::vec4(0, 0, -1, 0);
    fmt::print("camera: {}\n", view);
}
