#pragma once

#include "filemanager.hpp"
#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "Texture.h"
#include "plugins/BindLessDescriptorPlugin.hpp"
#include "camera_base.h"
#include "Prototypes.hpp"
#include "DisplacementMap.hpp"
#include "Barrier.hpp"

#include <memory>

struct GBuffer {
    Texture color;
    Texture depth;
};

struct TerrainInfo {
    float width{};
    float height{};
    float zMin{};
    float zMax{};
};

struct Context {
    VulkanDevice* device{};
    VulkanDescriptorPool* descriptorPool{};
    GBuffer* gBuffer{};
    BaseCameraController* camera{};
    BindlessDescriptor* bindlessDescriptor{};
    std::unique_ptr<Prototypes> prototypes;
    glm::vec3* lightDirection{};
    float lightIntensity{1};
    uint screenWidth;
    uint screenHeight;
    uint dmap_tex_index{~0u};
    uint dmap_normal_tex_index{~0u};
    uint dmap_shadow_tex_index{~0u};
    uint transmittanceTextureIndex{~0u};
    uint multiScatteringTextureIndex{~0u};
    uint skyViewTextureIndex{~0u};
    uint arealPerspectiveTextureIndex{~0u};
};