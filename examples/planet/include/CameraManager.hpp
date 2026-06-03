#pragma once

#include <Camera.h>

#include "constant_buffers.hpp"

using PlanetCameraController = std::conditional_t<UseDoublePrecisionPlanet, DoubleBaseCameraController, BaseCameraController>;
using PlanetFirstPersonCameraSettings = std::conditional_t<UseDoublePrecisionPlanet, DoubleFirstPersonSpectatorCameraSettings, FirstPersonSpectatorCameraSettings>;
using PlanetFirstPersonCameraController = std::conditional_t<UseDoublePrecisionPlanet, DoubleFirstPersonCameraController, FirstPersonCameraController>;
using PlanetFrustum = std::conditional_t<UseDoublePrecisionPlanet, DoubleFrustum, Frustum>;

class CameraManager {
public:
    CameraManager() = default;

    void initialize(InputManager& inputManager, const glm::ivec2& screenSize);

    void newFrame() {
        _camera->newFrame();
    }

    void update(float deltaTime) {
        _camera->update(deltaTime);
    }

    void processInput() {
        _camera->processInput();
    }

    void get(glm::mat4& view, glm::mat4 &viewProjection, glm::mat4 &invViewProjection, PlanetFrustum &frustum) const;

    auto position() const {
        return _camera->position();
    }

    auto near() const {
        return _camera->near();
    }

    auto far() const {
        return _camera->far();
    }

    auto fieldOfView() const {
        return _camera->fov;
    }

    auto cameraMatrix() const {
        return _camera->camera;
    }

    auto viewDirection() const {
        return _camera->viewDir;
    }

private:
    std::unique_ptr<PlanetCameraController> _camera;

};