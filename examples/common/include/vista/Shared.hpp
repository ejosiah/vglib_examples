#pragma once

#include "filemanager.hpp"
#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "Texture.h"
#include "plugins/BindLessDescriptorPlugin.hpp"
#include "camera_base.h"
#include "Prototypes.hpp"
#include "Barrier.hpp"

#include <memory>

struct RenderGraphInputs {
    Texture color;
    Texture position;
    Texture depth;
    Texture depth1;
};

struct TerrainInfo {
    float width{};
    float height{};
    float zMin{};
    float zMax{};
};
