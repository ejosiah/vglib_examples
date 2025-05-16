#pragma once

#include <glm/glm.hpp>

struct Domain3D{
    glm::vec3 lower{};
    glm::vec3 upper{};
};

struct UpdateInfo3D {
    uint32_t objectId;
    uint32_t pass;
    uint32_t tid;
    uint32_t cellID;
};


struct GlobalData3D {
    Domain3D domain;
    glm::vec3 gravity;
    float spacing;
    float halfSpacing;
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
    uint32_t screenWidth;
    uint32_t screenHeight;
};

struct Attribute3D {
    uint32_t objectID;
    uint32_t controlBits;
};

struct CellInfo3D {
    uint32_t index;
    uint32_t numHomeCells;
    uint32_t numPhantomCells;
    uint32_t numCells;
};

struct Emitter3D {
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
    static constexpr uint32_t Object = 0;
    static constexpr uint32_t CellID = 1;
    static constexpr uint32_t CellArrayIndex = 2;
    static constexpr uint32_t Count = 3;

    static constexpr uint32_t ObjectCmd = 0;
    static constexpr uint32_t CellIDCmd = sizeof(uint32_t) * 4;
    static constexpr uint32_t CellArrayIndexCmd = sizeof(uint32_t) * 8;
    static constexpr VkDeviceSize Size = sizeof(uint32_t) * 4 * Count;
};

struct ScratchPad3D {
    VulkanBuffer buffer;
    VkDeviceSize offset{0};
};