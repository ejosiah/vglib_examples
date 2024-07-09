#pragma once

#include "Texture.h"
#include "RingBuffer.hpp"
#include "plugins/BindLessDescriptorPlugin.hpp"

#include <VulkanDevice.h>
#include <VulkanBuffer.h>
#include <glm/glm.hpp>
#include <tiny_gltf.h>

#include <cinttypes>
#include <string>

namespace gltf {

    enum AlphaMode { OPAQUE_ = 0, MASK, BLEND };

    enum class ComponentType : int { BYTE = 5120, UNSIGNED_BYTE = 5121, SHORT = 5122, UNSIGNED_SHORT = 5123, UNSIGNED_INT = 5125, FLOAT = 5126  };

    enum class TextureType : int { BASE_COLOR = 0, NORMAL, METALLIC_ROUGHNESS, OCCLUSION, EMISSION};

    inline int indexOf(TextureType textureType) {
        return static_cast<int>(textureType);
    }

    struct ComponentTypes {
        static ComponentType valueOf(int v) { return static_cast<ComponentType>(v); }
    };

    glm::vec2 vec2From(const std::vector<double>& v);
    glm::vec3 vec3From(const std::vector<double>& v);
    glm::vec4 vec4From(const std::vector<double>& v);
    glm::quat quaternionFrom(const std::vector<double>& q);
    glm::mat4 getTransformation(const tinygltf::Node& node);

    struct Mesh{
        uint32_t meshId{};
        uint32_t indexCount{};
        uint32_t instanceCount{};
        uint32_t firstIndex{};
        uint32_t  vertexOffset{};
        uint32_t firstInstance{};
        uint32_t materialId{};
        ComponentType indexType{VK_INDEX_TYPE_UINT16};
    };

    struct MeshData {
        glm::mat4 model{1};
        glm::mat4 ModelInverse{1};
        int materialId{0};
        char padding[12]{};   // spirv-dis - ArrayStride 144 bytesclear
    };

    struct MaterialData {
        glm::vec4 baseColor{1.0};

        glm::vec3 emission{0};
        float alphaCutOff{0.5};

        float metalness{0};
        float roughness{1};
        int alphaMode{AlphaMode::OPAQUE_};
        int doubleSided{0};

        std::array<int, 8> textures{-1};
    };

    struct Draw {
        VulkanBuffer gpu;
        std::atomic_int count{};
    };

    struct DrawInstanceOffset {
        uint32_t drawId;
        int offset;
    };

    struct Model{
        friend class Loader;

        glm::mat4 transform{1};
        std::vector<Texture> textures;
        VulkanBuffer vertexBuffer;
        VulkanBuffer indexBufferUint16;
        VulkanBuffer indexBufferUint32;
        VulkanBuffer mesh16Buffer;
        VulkanBuffer mesh32Buffer;
        VulkanBuffer materialBuffer;
        VulkanBuffer instanceOffsetBuffer;
        uint32_t numMeshes{0};
        uint32_t numTextures{0};
        Draw draw_16;
        Draw draw_32;
        VkDescriptorSet mesh16descriptorSet{};
        VkDescriptorSet mesh32descriptorSet{};
        VkDescriptorSet materialDescriptorSet{};
        struct {
            glm::vec3 min{0};
            glm::vec3 max{0};
        } bounds;

        void sync() const;

        bool texturesLoaded() const;

        bool fullyLoaded() const;

    };

    struct PendingModel {
        VulkanDevice* device;
        BindlessDescriptor* bindlessDescriptor{};
        std::unique_ptr<tinygltf::Model> gltf{};
        std::vector<Mesh> meshes;
        std::shared_ptr<Model> model;
        std::filesystem::path path;
        int textureBindingOffset{0};

        void updateDrawState();

        void uploadMaterials();

    };

    struct UploadedTexture {
        std::shared_ptr<Model> model;
        uint32_t bindingId{};
        Texture texture;
    };

    struct TextureUploadRequest {
        std::shared_ptr<Model> model;
        std::string uri;
        uint32_t bindingId{};
        tinygltf::Image image{};
    };
}