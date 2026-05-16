#pragma once

#include "filemanager.hpp"
#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "Texture.h"
#include "plugins/BindLessDescriptorPlugin.hpp"
#include "camera_base.h"
#include "Prototypes.hpp"
#include "Barrier.hpp"

#include <glm/glm.hpp>
#include <memory>

struct RenderGraphInputs {
    Texture color;
    Texture position;
    Texture depth;
    Texture depth1;
};

struct TerrainInfo {
    glm::ivec2 terrainSize{};
    glm::vec2 heightScale{};
};
