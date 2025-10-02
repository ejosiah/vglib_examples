#pragma once

#include "SubdivisionGrid.hpp"
#include "camera_base.h"
#include "Prototypes.hpp"

class FFTOcean2 :public SubdivisionGrid {
public:
    FFTOcean2(VulkanDevice& device, VulkanDescriptorPool& descriptorPool, BindlessDescriptor& bindlessDescriptor,
              Prototypes& prototypes, BaseCameraController& camera, uint width, uint height);

    void init() final;

    void newFrame();

    void preProcess(VkCommandBuffer commandBuffer);

    void render(VkCommandBuffer commandBuffer);

    void endFrame();

protected:
    PipelineMetaData subdivisionMetadata() final;

    void subdivide(VkCommandBuffer commandBuffer, int pingPong) final;

    void createPipelines() final;

    void initUniforms();

    void createDescriptorSetLayout() final;

    void updateDescriptorSets() final;

    float computeLodFactor();

private:
    Prototypes* m_prototypes;
    BaseCameraController* m_camera;
    Pipeline m_render;

    struct UniformData {
        glm::mat4 modelMatrix{1};
        glm::mat4 modelViewMatrix{1};
        glm::mat4 viewMatrix{1};
        glm::mat4 cameraMatrix{1};
        glm::mat4 viewProjectionMatrix{1};
        glm::mat4 modelViewProjectionMatrix{1};
        std::array<glm::vec4, 6> frustumPlanes;
        glm::vec4 horizontalLength{1000, 200, 20, 400};
        glm::vec2 dimensions{52660};
        float lodFactor{0};
        float minLodVariance{0};
        float dmapFactor{1};
        uint heightMapIndex{~0u};
        uint normalMapIndex{~0u};
    } defaultValues{};

    struct {
        VulkanBuffer gpu;
        UniformData* cpu{};
    } m_uniforms;

    struct {
        float width{52660};
        float height{52660};
    } m_dimensions;
    VulkanDescriptorSetLayout m_uniformsDescriptorSetLayout;
    VkDescriptorSet m_uniformsDescriptorSet{};

    struct {
        float primitivePixelLengthTarget{7};
        float minLodStdev{0};
        float dmapScale{1};
        int gpuSubDivisions{3};
        bool topView{false};
        bool wire{false};
        bool showTiles{false};
    } m_options;

    uint m_heightMapIndex{~0u};
    uint m_normalIndex{~0u};
};