#pragma once

#include "ComputePipelins.hpp"
#include "Texture.h"
#include "ContextAware.hpp"

class ErosionSimulator {
public:
    enum class StepResult {
        Idle,
        Running,
        Finished
    };

    ErosionSimulator(Context& context, glm::uvec2 size, glm::vec2 terrainWorldSize = glm::vec2{1.0f}, float terrainHeightScale = 1.0f);

    void init();

    void controls(bool show = true);

    void controlsContent();

    void update(VkCommandBuffer commandBuffer, Texture& displacementMap);

    StepResult step(VkCommandBuffer commandBuffer, Texture& displacementMap);

    void run(VkCommandBuffer commandBuffer, Texture& displacementMap);

    uint velocityFieldTextureIndex() const;

protected:
    void resetControlValues();

    void clear(VkCommandBuffer commandBuffer);

    void runIteration(VkCommandBuffer commandBuffer, uint iteration);

    void createTextures();

    void createComputePipelines();

    void applyRain(VkCommandBuffer commandBuffer);

    void computeOutflowFlux(VkCommandBuffer commandBuffer);

    void computeWaterHeightChange(VkCommandBuffer commandBuffer);

    void computeSedimentCapacity(VkCommandBuffer commandBuffer);

    void erodeDepositSediment(VkCommandBuffer commandBuffer);

    void advectSediment(VkCommandBuffer commandBuffer);

    void evaporateWater(VkCommandBuffer commandBuffer);

    void computeThermalOutflow(VkCommandBuffer commandBuffer);

    void applyThermalErosion(VkCommandBuffer commandBuffer);

    void dispatch(VkCommandBuffer commandBuffer, const char* pipelineName);

    std::vector<PipelineMetaData> metadata();

private:
    const Context& m_context;
    glm::uvec2 m_size{};
    ComputePipelines m_compute;

    Texture m_terrainHeight; // b
    Texture m_waterHeight;  // d
    Texture m_sedimentAmount; // s
    Texture m_flux; // (fT, fR, fB, fL)
    Texture m_velocityField; // v
    Texture m_rain; // r(x,y)
    Texture m_localHardnessCoef; // R(x, y)
    Texture m_worksheet; // st+dt, C, dS, mode
    Texture m_thermalFlow0;
    Texture m_thermalFlow1;
    Texture* m_displacementMap{};
    float m_localHardness{0.8f};
    bool m_running{};
    bool m_restartRequested{};
    bool m_manualStepping{};
    bool m_stepRequested{};
    bool m_hydraulicErosion{true};
    bool m_thermalErosion{true};
    uint m_iteration{};

    struct Constants {
        glm::ivec2 terrainSize{};
        float timeStep{0.02}; // dt
        float rainScale{0.012}; // Kr
        float pipeArea{20}; // A
        float gravity{9.81}; // g
        float sedimentCapacity{1}; // Kc
        float thermalErosionRate{0.15}; // Kt
        float soilSuspensionRate{0.5}; // Ks
        float sedimentDepositionRate{1}; // Kd
        float sedimentSofteningRate{5}; // Kh
        float maximalErosionDepth{10}; // Kd_max
        float evaporationRate{0.001}; // Ke
        float minimumHardness{0.1}; // Rmin
        float talusAngleTangentCoeff{0.8}; // Ka
        float talusAngleTangentBias{0.1}; // Ki
        float terrainTexelSizeX{1.0f};
        float terrainTexelSizeY{1.0f};
        float terrainHeightScale{1.0f};
        uint iteration{0};
        uint maxIterations{1000};
        uint terrainHeightTextureIndex{~0u};
        uint waterHeightTextureIndex{~0u};
        uint sedimentAmountTextureIndex{~0u};
        uint fluxTextureIndex{~0u};
        uint velocityFieldTextureIndex{~0u};
        uint rainTextureIndex{~0u};
        uint localHardnessCoefTextureIndex{~0u};
        uint worksheetTextureIndex{~0u};
        uint thermalFlowTextureIndex0{~0u};
        uint thermalFlowTextureIndex1{~0u};
        uint terrainHeightImageIndex{~0u};
        uint waterHeightImageIndex{~0u};
        uint sedimentAmountImageIndex{~0u};
        uint fluxImageIndex{~0u};
        uint velocityFieldImageIndex{~0u};
        uint rainImageIndex{~0u};
        uint localHardnessCoefImageIndex{~0u};
        uint worksheetImageIndex{~0u};
        uint thermalFlowImageIndex0{~0u};
        uint thermalFlowImageIndex1{~0u};
    } m_constants;

};
