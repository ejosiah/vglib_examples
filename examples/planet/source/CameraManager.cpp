#include "CameraManager.hpp"

#include "constants.hpp"

void CameraManager::initialize(InputManager& inputManager, const glm::ivec2& screenSize) {
    PlanetFirstPersonCameraSettings cameraSettings;
    cameraSettings.fieldOfView = g_CameraFOV;
    cameraSettings.zNear = 1.0;
    cameraSettings.zFar =  20000;
    cameraSettings.acceleration = PlanetVec3(50 * km);
    cameraSettings.velocity = PlanetVec3(200 * km);
    cameraSettings.aspectRatio = static_cast<PlanetScalar>(screenSize.x) / static_cast<PlanetScalar>(screenSize.y);

    _camera = std::make_unique<PlanetFirstPersonCameraController>(inputManager, cameraSettings);
    PlanetVec3 pos{0, 0, -(static_cast<PlanetScalar>(g_EarthRadius) + PlanetScalar(100.0f))};
    auto target = pos + PlanetVec3{0.0011957388, 0.9735858440, 0.2283589840};
    _camera->lookAt(pos, target, PlanetVec3{0, 0, -1});
}

void CameraManager::get(glm::mat4& view, glm::mat4 &viewProjection, glm::mat4 &invViewProjection, PlanetFrustum &frustum) const {
    const auto cam = _camera->camera;
    view = cam.view;
    view[3] = PlanetVec4{0, 0, 0, 1};
    auto projection = cam.proj;
    viewProjection = projection * view;
    auto modelViewProjection = viewProjection;
    invViewProjection = glm::inverse(viewProjection);

    PlanetFrustum::extractFrustum(frustum, modelViewProjection);
}
