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

namespace gltf {

    enum AlphaMode { OPAQUE_ = 0, MASK, BLEND };

    enum class ComponentType : int { BYTE = 5120, UNSIGNED_BYTE = 5121, SHORT = 5122, UNSIGNED_SHORT = 5123, UNSIGNED_INT = 5125, FLOAT = 5126  };

    enum class TextureType : int { BASE_COLOR = 0, NORMAL, METALLIC_ROUGHNESS, OCCLUSION, EMISSION};

    struct TextureTypes final {

        static int ordinal(TextureType type) { return static_cast<int>(type);  }
        static TextureType valueOf(int v) { return static_cast<TextureType>(v); }

    };


    struct ComponentTypes {
        static int ordinals(ComponentType type) { return static_cast<int>(type);  }
        static ComponentType valueOf(int v) { return static_cast<ComponentType>(v); }
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

    struct Model {
        friend class Loader;

        friend void sync(std::shared_ptr<Model> model);

        glm::mat4 transform{1};
        std::vector<Texture> textures{};
        Indices indices{};
        VulkanBuffer vertices{};
        VulkanBuffer materials;
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

        void sync() {
            _loaded.wait();
        }

    private:
        Condition _loaded;
    };

}

