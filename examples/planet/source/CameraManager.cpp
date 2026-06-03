#include "CameraManager.hpp"

#include "constants.hpp"

void CameraManager::initialize(InputManager& inputManager, const glm::ivec2& screenSize) {
    PlanetCameraSettings cameraSettings;
    cameraSettings.fieldOfView = g_CameraFOV;
    cameraSettings.zNear = 1.0;
    cameraSettings.zFar =  200000;
    cameraSettings.acceleration = PlanetVec3(50 * km);
    cameraSettings.velocity = PlanetVec3(200 * km);
    cameraSettings.aspectRatio = static_cast<PlanetScalar>(screenSize.x) / static_cast<PlanetScalar>(screenSize.y);
    cameraSettings.mode = m_CurrentMode;

    m_camera = std::make_unique<PlanetCameraController>(inputManager, cameraSettings);
    PlanetVec3 pos{0, 0, -(static_cast<PlanetScalar>(g_EarthRadius) + PlanetScalar(100.0f))};
    auto target = pos + PlanetVec3{0.0011957388, 0.9735858440, 0.2283589840};
    m_camera->lookAt(pos, target, PlanetVec3{0, 0, -1});

    evaluateDistances();
    evaluateClipPlanes();
}

void CameraManager::update(float deltaTime) {
    m_camera->update(deltaTime);
    evaluateDistances();
    evaluateClipPlanes();
}

void CameraManager::get(glm::mat4& view, glm::mat4 &viewProjection, glm::mat4 &invViewProjection, PlanetFrustum &frustum) const {
    const auto cam = m_camera->cameraMatrix();
    view = cam.view;
    view[3] = PlanetVec4{0, 0, 0, 1};
    auto projection = cam.proj;
    viewProjection = projection * view;
    auto modelViewProjection = viewProjection;
    invViewProjection = glm::inverse(viewProjection);

    PlanetFrustum::extractFrustum(frustum, modelViewProjection);
}

void CameraManager::evaluateDistances() {
    const auto position = m_camera->position();
    m_DistanceToPlanetCenter.x = glm::distance(position, CameraController::Vec3(g_EarthCenter));
    m_DistanceToPlanetCenter.y = glm::distance(position, CameraController::Vec3(g_MoonCenter));
}

void CameraManager::evaluateClipPlanes() {
    PlanetScalar nearP, farP;

    if (m_ClippingMode == ClippingMode::Automatic) {
        // Elevation
        PlanetScalar earthElevation = m_DistanceToPlanetCenter.x - g_EarthRadius;
        PlanetScalar moonElevation = m_DistanceToPlanetCenter.y - g_MoonRadius;

        if (earthElevation < 2000.0 || moonElevation < 5000.0f) {
            nearP = 0.1f;
            farP = 200000.0f;
        }
        else {
            PlanetScalar minElevation = std::min(earthElevation, moonElevation);
            nearP = minElevation / 50.0;
            PlanetScalar earthFar = std::max(sqrt(m_DistanceToPlanetCenter.x * m_DistanceToPlanetCenter.x - g_EarthRadius * g_EarthRadius) * 2.0, 100.0);
            PlanetScalar moonFar = std::max(sqrt(m_DistanceToPlanetCenter.y * m_DistanceToPlanetCenter.y - g_MoonRadius * g_MoonRadius) * 2.0, 100.0);
            farP = glm::min(earthFar, moonFar);
        }
    } else {
        nearP = m_camera->near();
        farP = m_camera->far();
    }

    auto fov = m_camera->fieldOfView();
    auto aspect = m_camera->aspectRatio();
    m_camera->perspective(fov, aspect, nearP, farP);
}
