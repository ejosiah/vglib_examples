#pragma once

#include "filemanager.hpp"
#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "Texture.h"
#include "plugins/BindLessDescriptorPlugin.hpp"
#include "camera_base.h"
#include "Prototypes.hpp"

#include <memory>

struct GBuffer {
    Texture color;
    Texture depth;
};

struct Context {
    VulkanDevice* device{};
    VulkanDescriptorPool* descriptorPool{};
    GBuffer* gBuffer{};
    BaseCameraController* camera{};
    BindlessDescriptor* bindlessDescriptor{};
    std::unique_ptr<Prototypes> prototypes;
    uint screenWidth;
    uint screenHeight;
    uint dmap_tex_index{~0u};
    uint dmap_normal_tex_index{~0u};
};