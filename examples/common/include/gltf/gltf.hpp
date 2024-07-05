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

    enum class TextureType : int { BASE_COLOR = 0, NORMAL, METALLIC_ROUGHNESS, OCCLUSION, EMISSION};

    inline int indexOf(TextureType textureType) {
        return static_cast<int>(textureType);
    }

    struct Mesh{
        uint32_t meshId{};
        uint32_t indexCount{};
        uint32_t instanceCount{};
        uint32_t firstIndex{};
        uint32_t  vertexOffset{};
        uint32_t firstInstance{};
        uint32_t materialId{};
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

    struct PendingModel;

    struct Model{
        friend class Loader;

        std::vector<Texture> textures;
        VulkanBuffer vertexBuffer;
        VulkanBuffer indexBuffer;
        VulkanBuffer meshBuffer;
        VulkanBuffer materialBuffer;
        uint32_t numMeshes{0};
        Draw draw;
        VkDescriptorSet descriptorSet{};
        struct {
            glm::vec3 min{0};
            glm::vec3 max{0};
        } bounds;

        void sync() const;

    };

    struct PendingModel {
        VulkanDevice* device;
        BindlessDescriptor* bindlessDescriptor{};
        std::unique_ptr<tinygltf::Model> gltf{};
        RingBuffer<Mesh> meshes;
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
        uint32_t bindingId{};
        tinygltf::Image image{};
    };
}