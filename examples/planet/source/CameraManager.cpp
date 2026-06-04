#include "CameraManager.hpp"

#include "imgui.h"
#include "constants.hpp"
#include "filemanager.hpp"

#include <algorithm>

namespace {
    const char* g_CameraModeNames[] = { "Flight", "FPS" };
    const char* g_ClippingModeNames[] = { "Automatic", "Manual" };
}

void CameraManager::initialize(InputManager& inputManager, const glm::ivec2& screenSize) {
    m_ControlPoints.resize(2);
    m_Duration = 1.0;
    m_IsPlaying = false;
    m_PlayTime = 0.0f;
    m_scaleOffset = {};
    m_angles = { 0.1, PI / 4.2, 0.0 };

    PlanetCameraSettings cameraSettings;
    cameraSettings.fieldOfView = g_CameraFOV;
    cameraSettings.zNear = 0.1;
    cameraSettings.zFar =  100;
    cameraSettings.acceleration = PlanetVec3(50 * km);
    cameraSettings.velocity = PlanetVec3(200 * km);
    cameraSettings.aspectRatio = static_cast<PlanetScalar>(screenSize.x) / static_cast<PlanetScalar>(screenSize.y);
    cameraSettings.mode = CameraMode::FLIGHT;

    m_Speed = 1.0f;

    m_camera = std::make_unique<PlanetCameraController>(inputManager, cameraSettings);
    PlanetVec3 pos{0, 0, -(static_cast<PlanetScalar>(g_EarthRadius) * PlanetScalar(40.0f))};

    m_camera->rotate(glm::degrees(m_angles.x), glm::degrees(m_angles.y), 0);

    auto view = glm::mat3(m_camera->cameraMatrix().view);
    pos = view * pos;
    auto up = glm::vec3{ -0.073183073, 0.680172738, -0.729389666};
    m_camera->lookAt(pos, glm::vec3(0), up);

    evaluateDistances();
    evaluateCameraMatrices();
}

bool CameraManager::newFrame() {
    m_camera->newFrame();

    if (m_IsPlaying){
        m_FrameIndex++;
        if (m_FrameIndex == 1)
            return true;
        return false;
    }
    return false;
}

void CameraManager::save_camera_path(const fs::path &path) {
    std::ofstream pathFile;
    pathFile.open(m_PathsDir / path);
    pathFile << m_ControlPoints.size() << std::endl;
    pathFile << m_Duration << std::endl;
    for (uint32_t ptIdx = 0; ptIdx < m_ControlPoints.size(); ++ptIdx) {
        const Transform& cT = m_ControlPoints[ptIdx];
        pathFile << cT.rotation.x <<";" << cT.rotation.y << ";" << cT.rotation.z << ";" << cT.rotation.w
            << ";" << cT.position.x << ";" << cT.position.y << ";" << cT.position.z
            << ";" << cT.angles.x << ";" << cT.angles.y << ";" << cT.angles.z << ";" << std::endl;
    }
    pathFile.close();
}

void CameraManager::changeMode(CameraMode newMode) {
}

uint32_t CameraManager::play_frame_index() const {
    return m_FrameIndex / 2;
}

void CameraManager::renderUI() {
    ImGui::Separator();
    auto numControlsPoints = static_cast<int32_t>(m_ControlPoints.size());
    float totalSize = 340.0f + numControlsPoints * 23.0f;
    ImGui::SetNextWindowSize(ImVec2(550.0f, totalSize));
    ImGui::Begin("Camera controls");


    static auto pos = glm::vec3{m_camera->position()};
    static auto fov = m_camera->fieldOfView();
    static auto scaleOffset = glm::vec3{};
    static auto nearFar = glm::vec2{m_camera->near(), m_camera->far()};
    if (ImGui::CollapsingHeader("Base Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Base properties
        ImGui::InputFloat("Position", &pos.x);
        ImGui::InputFloat3("Angles", &m_angles.x);
        ImGui::SliderFloat("FOV", &fov, 0.001, 0.8);
        ImGui::InputFloat3("Scale Offset", &scaleOffset.x);

        // Camera speed
        ImGui::InputFloat2("Near/Far", &nearFar.x);
    }

    if (ImGui::CollapsingHeader("Control", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Previous mode
        CameraMode newCameraMode = m_camera->mode();
        const auto modeIndex = newCameraMode == CameraMode::FLIGHT ? 0 : 1;
        const char* currentCameraItem = g_CameraModeNames[modeIndex];
        if (ImGui::BeginCombo("Control Mode", currentCameraItem)) // The second parameter is the label previewed before opening the combo.
        {
            for (int n = 0; n < IM_ARRAYSIZE(g_CameraModeNames); n++)
            {
                bool is_selected = (currentCameraItem == g_CameraModeNames[n]); // You can store your selection however you want, outside or inside your objects
                if (ImGui::Selectable(g_CameraModeNames[n], is_selected))
                {
                    newCameraMode = n == 0 ? CameraMode::FLIGHT :CameraMode::FIRST_PERSON;
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();   // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
                }
            }
            ImGui::EndCombo();
        }
        if (newCameraMode != m_camera->mode()) {
            m_camera->setMode(newCameraMode);


            // Previous mode
            const char* currentClippingItem = g_ClippingModeNames[(uint32_t)m_ClippingMode];
            if (ImGui::BeginCombo("Clipping Mode", currentClippingItem)) // The second parameter is the label previewed before opening the combo.
            {
                for (int n = 0; n < IM_ARRAYSIZE(g_ClippingModeNames); n++)
                {
                    bool is_selected = (currentClippingItem == g_ClippingModeNames[n]); // You can store your selection however you want, outside or inside your objects
                    if (ImGui::Selectable(g_ClippingModeNames[n], is_selected))
                    {
                        m_ClippingMode = (ClippingMode)n;
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();   // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
                    }
                }
                ImGui::EndCombo();
            }

            if (m_ClippingMode == ClippingMode::Manual)
            {
                ImGui::InputFloat2("Near/Far", &nearFar.x);
            }
        }
    }

    // Path
    if (ImGui::CollapsingHeader("Path", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Num of control points
        ImGui::PushItemWidth(100);
        ImGui::InputInt("Control Points", &numControlsPoints);
        numControlsPoints = std::max(2, numControlsPoints);
        m_ControlPoints.resize(numControlsPoints);
        ImGui::SameLine();
        ImGui::InputFloat("Duration", &m_Duration);
        ImGui::Checkbox("Loop", &m_LoopAnimation);
        ImGui::PopItemWidth();
    }

    // Play the path
    ImGui::SameLine();
    if (ImGui::Button("Play"))
        setup_play_path();
    ImGui::SameLine();
    if (ImGui::Button("Stop"))
        stop_play_path();

    auto inoutFile =  FileManager::instance().getFullPath("loop_demo.csv");
    if (inoutFile.has_value()) {
        auto label = inoutFile.value().filename().string();
        ImGui::PushItemWidth(160);
        ImGui::InputText("Filename", const_cast<char*>(label.c_str()), 256);
        ImGui::PopItemWidth();

        ImGui::SameLine();
        if (ImGui::Button("Save Path"))
            save_camera_path(inoutFile.value());

        ImGui::SameLine();
        if (ImGui::Button("Load Path"))
            loadpath(inoutFile.value());
    }


    // Display the control points
    for (int32_t controlPointIdx = 0; controlPointIdx < numControlsPoints; ++controlPointIdx)
    {
        Transform& currentTransform = m_ControlPoints[controlPointIdx];

        ImGui::PushItemWidth(175.0f);
        ImGui::InputScalarN("Rot", ImGuiDataType_Float, &currentTransform.rotation.x, 4);
        ImGui::PopItemWidth();

        ImGui::SameLine();
        ImGui::PushItemWidth(125.0f);
        ImGui::InputScalarN("Pos", ImGuiDataType_Double, &currentTransform.position.x, 3);
        ImGui::PopItemWidth();

        std::string label = "Copy ";
        label += std::to_string(controlPointIdx);
        ImGui::SameLine();
        if (ImGui::Button(label.c_str()))
        {
            currentTransform.position = m_camera->position();
            auto quat = glm::quat_cast(m_camera->cameraMatrix().view);
            currentTransform.rotation = quat;
            currentTransform.angles = m_angles;
        }

        label = "Apply ";
        label += std::to_string(controlPointIdx);
        ImGui::SameLine();
        if (ImGui::Button(label.c_str()))
        {
            m_camera->position(currentTransform.position);
            m_angles = currentTransform.angles;
        }
    }

    m_camera->position(pos);
    m_camera->fieldOfView(fov);
    m_camera->near(nearFar.x);
    m_camera->far(nearFar.y);

    ImGui::End();
}

void CameraManager::update(float deltaTime) {
    evaluateDistances();
    evaluateCameraMatrices();
    m_camera->update(deltaTime);
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

void CameraManager::evaluateCameraMatrices() {
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

void CameraManager::loadpath(const fs::path &path) {
    std::ifstream pathFile;
    pathFile.open(m_PathsDir / path);
    if (!pathFile.is_open())
        return;

    // Read the number of control points
    uint32_t numPoints;
    pathFile >> numPoints;
    m_ControlPoints.resize(numPoints);

    // Read the duration
    pathFile >> m_Duration;

    // Read the individual points
    std::string line;
    for (uint32_t ptIdx = 0; ptIdx < numPoints; ++ptIdx)
    {
        // Read the line
        pathFile >> line;

        // Decompose it
        std::istringstream iss(line);
        Transform& cT = m_ControlPoints[ptIdx];
        char s;
        iss >> cT.rotation.x >> s >> cT.rotation.y >> s >> cT.rotation.z >> s >> cT.rotation.w
            >> s >> cT.position.x >> s >> cT.position.y >> s >> cT.position.z
            >> s >> cT.angles.x >> s >> cT.angles.y >> s >> cT.angles.z;
    }
    pathFile.close();
}

void CameraManager::setup_play_path() {
    // Mark that we are playing
    m_IsPlaying = true;

    // Reset the current play time
    m_PlayTime = 0.0;
    m_FrameIndex = 0;

    // Keep track of the previous position
    m_SavedPosition = m_camera->position();

    // Allocate the buffers at the right sizes
    auto numControlPoints = static_cast<uint32_t>(m_ControlPoints.size());
    m_PositionSpline.resize(numControlPoints);
    m_RotationSpline.resize(numControlPoints);

    // Save the points
    for (uint32_t ptIdx = 0; ptIdx < numControlPoints; ++ptIdx)
    {
        m_PositionSpline[ptIdx] = m_ControlPoints[ptIdx].position;
        m_RotationSpline[ptIdx] = m_ControlPoints[ptIdx].rotation;

        // Sanitize the rotation
        if (ptIdx > 0)
        {
            float l0 = glm::length(m_RotationSpline[ptIdx] - m_RotationSpline[ptIdx - 1]);
            float l1 = glm::length(m_RotationSpline[ptIdx] + m_RotationSpline[ptIdx - 1]);
            if (l1 < l0)
                m_RotationSpline[ptIdx] = -m_RotationSpline[ptIdx];
        }
    }
}

void CameraManager::stop_play_path() {
    m_IsPlaying = false;
    m_camera->position(m_SavedPosition);
}
