#pragma once

#include "Texture.h"
#include "RingBuffer.hpp"
#include "plugins/BindLessDescriptorPlugin.hpp"
#include "Condition.hpp"

#include <VulkanDevice.h>
#include <VulkanBuffer.h>
#include <glm/glm.hpp>
#include <tiny_gltf.h>

#include <cinttypes>
#include <string>
#include <memory>

#define INT_VALUE(Enum)

namespace gltf {

    enum AlphaMode { OPAQUE_ = 0, MASK, BLEND };

    enum class ComponentType : int { BYTE = 5120, UNSIGNED_BYTE = 5121, SHORT = 5122, UNSIGNED_SHORT = 5123, UNSIGNED_INT = 5125, FLOAT = 5126  };

    enum class TextureType : int { BASE_COLOR = 0, NORMAL, METALLIC_ROUGHNESS, OCCLUSION, EMISSION, THICKNESS };

    enum class LightType : int { DIRECTIONAL = 0, POINT, SPOT };

    struct TextureTypes final {

        static int ordinal(TextureType type) { return static_cast<int>(type);  }
        static TextureType valueOf(int v) { return static_cast<TextureType>(v); }

    };

    struct ComponentTypes final {
        static int ordinal(ComponentType type) { return static_cast<int>(type);  }
        static ComponentType valueOf(int v) { return static_cast<ComponentType>(v); }
    };

    struct LightTypes final {
        static int ordinal(LightType type) { return static_cast<int>(type); }
        static LightType valueOf(int v) { return static_cast<LightType>(v); }
    };

    struct Draw {
        struct {
            VulkanBuffer handle;
            std::atomic_uint32_t count{};
        } u16;
        struct {
            VulkanBuffer handle;
            std::atomic_int count{};
        } u32;
    };

    struct Buffer {
        struct {
            VulkanBuffer handle;
            BufferRegion rHandle;
        } u16;
        struct {
            VulkanBuffer handle;
            BufferRegion rHandle;
        } u32;
    };

    struct MeshDescriptorSet {
        struct {
            VkDescriptorSet handle;
        } u16;
        struct {
            VkDescriptorSet handle;
        } u32;
    };

    using Indices = Buffer;
    using Meshes = Buffer;

    struct Light {
        glm::vec3 direction{0, 0, -1};
        float range{0};

        glm::vec3 color{1};
        float intensity{1};

        glm::vec3 position{0};
        float innerConeCos{0};

        float outerConeCos{0};
        int type{LightTypes::ordinal(LightType::DIRECTIONAL)};
        glm::vec2 padding;
    };

    struct Model {
        friend class Loader;

        friend void sync(std::shared_ptr<Model> model);

        glm::mat4 transform{1};
        std::vector<Texture> textures{};
        Indices indices{};
        VulkanBuffer vertices{};
        VulkanBuffer materials;
        VulkanBuffer lights;
        VulkanBuffer lightInstances;
        std::vector<glm::mat4> placeHolders;
        Meshes meshes{};
        Draw draw;
        MeshDescriptorSet meshDescriptorSet{};
        VkDescriptorSet materialDescriptorSet{};
        struct {
            glm::vec3 min{0};
            glm::vec3 max{0};
        } bounds;
        uint32_t numTextures{0};
        uint32_t numMeshes{0};
        uint32_t numLights{0};

        void sync() {
            _loaded.wait();
        }

    private:
        Condition _loaded;
    };

}

