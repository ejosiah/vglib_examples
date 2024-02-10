#pragma once

#include <VulkanDevice.h>
#include <Texture.h>

#include <taskflow/taskflow.hpp>

#include <filesystem>
#include <atomic>
#include <functional>
#include <openvdb/openvdb.h>

struct Voxel {
    glm::vec3 position;
    float value;
};

struct Bounds {
    glm::vec3 min{0}, max{0};
};

struct Volume {
    int id{-1};
    float invMaxDensity{0};
    Bounds bounds;
    openvdb::GridCPtrVec grids;
};


struct Frame {
    int index{0};
    int elapsedMS{};
    int durationMS{std::numeric_limits<int>::max()};
    Volume volume;
};

class VdbAnimation;

using FramesReady = std::function<void()>;
using FrameLoadFailed = std::function<void()>;

class VdbAnimation {
public:
    VdbAnimation() = default;

    VdbAnimation(VulkanDevice* device, int frameRate = 24);

    void init();

    void update(float timeSeconds);

    void createDescriptorPool();

    void createDescriptorSetLayout();

    void updateDescriptorSet();

    void load(const std::filesystem::path& path);

    void setFrameRate(int value);

    bool advanceFrame(VkDescriptorSet dstSet, uint32_t dstBinding) const;

    void prepareFrame(uint32_t index) const;

    const Frame& frame(uint32_t index) const;

    const Frame& currentFrame() const;

    const bool ready() const;

    void onFramesReady(FramesReady&& framesReady);

    void onFrameLoadFailed(FrameLoadFailed&& framesLoadFailed);

    glm::vec3 minBounds() const;

    glm::vec3 maxBounds() const;

    float inverseMaxDensity() const;

private:
    Frame loadFrame(fs::path path);

private:
    static constexpr uint32_t MaxFrames = std::numeric_limits<int>::max();
    static constexpr uint32_t PathsPerQueue = 90;

    VulkanDevice* device_;
    int framePeriod_;
    FramesReady framesReady_;
    FrameLoadFailed frameLoadFailed_;

    std::atomic_bool ready_{false};
    VulkanBuffer staging_;
    std::vector<Frame> frames_;
    uint32_t frameIndex_;
    mutable bool shouldAdvanceFrame_{};

    Bounds bounds_;
    tf::Executor executor_;
    tf::Taskflow loadVolumeFlow_{};
    tf::Future<void> loadVolumeRequest_;

    Texture stagingTexture_;
    VulkanDescriptorPool descriptorPool_;
    VulkanDescriptorSetLayout descriptorSetLayout_;
    VkDescriptorSet descriptorSet_;
};