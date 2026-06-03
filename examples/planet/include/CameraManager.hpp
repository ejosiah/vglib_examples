#pragma once

#include <Camera.h>

#include "constants.hpp"
#include "constant_buffers.hpp"

using PlanetCameraController = std::conditional_t<UseDoublePrecisionPlanet, DoubleCameraController, CameraController>;
using PlanetCameraSettings = std::conditional_t<UseDoublePrecisionPlanet, DoubleCameraSettings, CameraSettings>;
using PlanetFrustum = std::conditional_t<UseDoublePrecisionPlanet, DoubleFrustum, Frustum>;

enum class ClippingMode {
    Automatic = 0,
    Manual
};

enum class NavigationButtons
{
    Forward = 0,
    Backward,
    Left,
    Right,
    Up,
    Down,
    Shift,
    Count
};


struct Transform {
    glm::quat rotation;
    PlanetVec3 position;
    glm::vec3 angles;
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

    // Button controls
    bool m_ControllerStates[(uint32_t)NavigationButtons::Count] = { false, false, false, false, false, false, false };

    // Flag that defines if we can interact with the camera
    bool m_ActiveInteraction = false;
    std::string m_PathsDir;

    // Speed
    float m_Speed = 0.0f;

    // Duration
    float m_Duration = 0.0f;

    // Is playing
    bool m_IsPlaying = false;
    bool m_LoopAnimation = true;
    float m_PlayTime = 0.0f;
    uint32_t m_FrameIndex = 0;
    glm::dvec3 m_SavedPosition = {};
    std::vector<glm::dvec3> m_PositionSpline;
    std::vector<glm::vec3> m_RotationSpline;

    std::vector<Transform> m_ControlPoints = {};

    CameraMode m_CurrentMode = CameraMode::FIRST_PERSON;
    glm::vec3 m_angles{-0.1, -pi / 4.2, 0.0 };
};