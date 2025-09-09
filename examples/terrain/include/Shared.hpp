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
    Texture normal;
    Texture position;
    Texture depth;
};

struct RenderGraphInputs {
    Texture color;
    Texture position;
    Texture depth;
};

struct TerrainInfo {
    float width{};
    float height{};
    float zMin{};
    float zMax{};
};