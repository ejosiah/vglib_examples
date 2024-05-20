#pragma once

#include "ComputePipelins.hpp"
#include "Barrier.hpp"
#include "VulkanDevice.h"
#include "FixedUpdate.hpp"
#include "Profiler.hpp"
#include "Vertex.h"
#include "Cloth.hpp"
#include "Geometry.hpp"

#include <memory>

class Integrator : public ComputePipelines {
public:
    struct {
        glm::vec2 inv_cloth_size;
        float timeStep{0.01666667};
        float mass = 1.0;
        float ksStruct = 100000;
        float ksShear = 25000;
        float ksBend = 25000;
        float kdStruct = 5.5f;
        float kdShear = 0.25f;
        float kdBend = 0.25f;
        float kd = 5.f;
        float elapsedTime = 0;
        int simWind{0};
        float gravityY{-9.8f};
        float windStrength{1};
        float windSpeed{1};
    } constants{};

public:
    Integrator() = default;

    Integrator(VulkanDevice& device,
               VulkanDescriptorPool& descriptorPool,
               std::shared_ptr<Cloth> cloth,
               std::shared_ptr<Geometry> geometry,
               int fps = 480);

    void init();

    void integrate(VkCommandBuffer commandBuffer);

    void update(float dt);

    void onFrameEnd();

protected:
    std::vector<PipelineMetaData> pipelineMetaData() final;

    virtual void init0() = 0;

    virtual void integrate0(VkCommandBuffer commandBuffer) = 0;

    virtual std::vector<PipelineMetaData> pipelineMetaData0() = 0;

    VulkanDescriptorPool& descriptorPool();

private:
    void initializeBuffers();

    void initDescriptorSetLayout();

    void initDescriptorSets();

protected:
    VulkanDescriptorPool* _descriptorPool{};
    std::shared_ptr<Cloth> _cloth;
    FixedUpdate _fixedUpdate;
    Profiler _profiler;
    VulkanBuffer _points;
    std::shared_ptr<Geometry> _geometry;

    VulkanDescriptorSetLayout _attributesSetLayout;
    VkDescriptorSet _attributesSet{};
    VulkanDescriptorSetLayout _geometrySetLayout;
    VkDescriptorSet _geometrySet{};


};