#pragma once

#include <glm/glm.hpp>

struct Domain{
    glm::vec3 lower{};
    glm::vec3 upper{};
};

inline Domain expand(Domain domain, float factor) {
    Domain newDomain = domain;
    newDomain.lower -= factor;
    newDomain.upper += factor;

    return newDomain;
}

struct UpdateInfo {
    uint32_t objectId;
    uint32_t pass;
    uint32_t tid;
    uint32_t cellID;
};


struct GlobalData {
    Domain domain;
    glm::vec3 gravity;
    glm::vec3 light;
    float spacing;
    float time;
    uint32_t numObjects;
    uint32_t gridSize;
    uint32_t numCells;
    uint32_t segmentSize;
    uint32_t numCellIndices;
    uint32_t numEmitters;
    uint32_t numSphereEmitters;
    uint32_t numUpdates;
    uint32_t frame;
    uint32_t numDistanceConstraints;
    float restitution;
};

struct Attribute {
    uint32_t objectID;
    uint32_t controlBits;
};

struct CellInfo {
    uint32_t index;
    uint32_t numHomeCells;
    uint32_t numPhantomCells;
    uint32_t numCells;
};

struct Emitter {
    glm::vec3 origin;
    glm::vec3 direction;
    float radius;
    float offset;
    float speed;
    float spreadAngleRad;
    int maxNumberOfParticlePerSecond;
    int maxNumberOfParticles;
    float firstFrameTimeInSeconds;
    float currentTime;
    int numberOfEmittedParticles;
    int disabled;
};

namespace Dispatch {
    static constexpr auto DispatchSize = sizeof(VkDispatchIndirectCommand);
    static constexpr uint32_t Object = 0;
    static constexpr uint32_t CellID = 1;
    static constexpr uint32_t CellArrayIndex = 2;
    static constexpr uint32_t Count = 3;

    static constexpr uint32_t ObjectCmd = 0;
    static constexpr uint32_t CellIDCmd = DispatchSize;
    static constexpr uint32_t CellArrayIndexCmd = DispatchSize * 2;
    static constexpr VkDeviceSize Size = DispatchSize * Count;
};

struct ScratchPad {
    VulkanBuffer buffer;
    VkDeviceSize offset{0};
};

struct DistanceConstraint {
    uint32_t a;
    uint32_t b;
    float l;
};