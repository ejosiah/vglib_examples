#pragma once

#include <Camera.h>

#include "constant_buffers.hpp"

using PlanetCameraController = std::conditional_t<UseDoublePrecisionPlanet, DoubleCameraController, CameraController>;
using PlanetCameraSettings = std::conditional_t<UseDoublePrecisionPlanet, DoubleCameraSettings, CameraSettings>;
using PlanetFrustum = std::conditional_t<UseDoublePrecisionPlanet, DoubleFrustum, Frustum>;

enum class ClippingMode {
    Automatic = 0,
    Manual
};


class CameraManager {
public:
    CameraManager() = default;

    void initialize(InputManager& inputManager, const glm::ivec2& screenSize);

    void newFrame() {
        m_camera->newFrame();
    }

    void update(float deltaTime);

    void processInput() {
        m_camera->processInput();
    }

    void get(glm::mat4& view, glm::mat4 &viewProjection, glm::mat4 &invViewProjection, PlanetFrustum &frustum) const;

    [[nodiscard]] auto position() const {
        return m_camera->position();
    }

    [[nodiscard]] auto near() const {
        return m_camera->near();
    }

    [[nodiscard]] auto far() const {
        return m_camera->far();
    }

    [[nodiscard]] auto fieldOfView() const {
        return m_camera->fieldOfView();
    }

    [[nodiscard]] auto cameraMatrix() const {
        return m_camera->cameraMatrix();
    }

    [[nodiscard]] auto viewDirection() const {
        return m_camera->viewDirection();
    }

    void evaluateDistances();

    void evaluateClipPlanes();

private:
    std::unique_ptr<CameraController> m_camera;
    PlanetVec2 m_DistanceToPlanetCenter{};
    ClippingMode m_ClippingMode{ClippingMode::Automatic};

};