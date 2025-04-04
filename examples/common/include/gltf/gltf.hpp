#pragma once

#include "Texture.h"
#include "RingBuffer.hpp"
#include "plugins/BindLessDescriptorPlugin.hpp"
#include "concurrency/Condition.hpp"

#include <VulkanDevice.h>
#include <VulkanBuffer.h>
#include <glm/glm.hpp>
#include <tiny_gltf.h>

#include <cinttypes>
#include <string>
#include <memory>


namespace gltf {

    enum AlphaMode { OPAQUE_ = 0, MASK, BLEND };

    enum class Mode: int { POINTS = 0, LINES, LINE_LOOP, LINE_STRIP, TRIANGLES, TRIANGLE_STRIP, TRIANGLE_FAN  };

    enum class ComponentType : int { BYTE = 5120, UNSIGNED_BYTE = 5121, SHORT = 5122, UNSIGNED_SHORT = 5123, UNSIGNED_INT = 5125, FLOAT = 5126 , UNDEFINED };

    enum class TextureType : int { BASE_COLOR = 0, NORMAL, METALLIC_ROUGHNESS, OCCLUSION
                                 , EMISSION, THICKNESS, CLEAR_COAT_COLOR, CLEAR_COAT_ROUGHNESS
                                 , CLEAR_COAT_NORMAL, SHEEN_COLOR, SHEEN_ROUGHNESS, ANISOTROPY,
                                  SPECULAR_STRENGTH, SPECULAR_COLOR, IRIDESCENCE, IRIDESCENCE_THICKNESS,
                                  TRANSMISSION };

    enum class LightType : int { DIRECTIONAL = 0, POINT, SPOT };

    constexpr int NUM_TEXTURE_MAPPING = 20;


    struct Draw {
        struct {
            VulkanBuffer handle;
            std::atomic_uint32_t count{};
        } u8;
        struct {
            VulkanBuffer handle;
            std::atomic_uint32_t count{};
        } u16;
        struct {
            VulkanBuffer handle;
            std::atomic_int count{};
        } u32;
        Mode mode{Mode::TRIANGLES};
    };

    struct Buffer {
        struct {
            VulkanBuffer handle;
            BufferRegion rHandle;
        } u8;
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
        } u8;
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
        glm::vec3 position{};
        glm::vec3 direction{0, 0, -1};
        glm::vec3 color{1};
        float range{};
        float intensity{1};
        float innerConeCos{};
        float outerConeCos{};
        int type{to<int>(LightType::DIRECTIONAL)};
        int shadowMapIndex{-1};
    };

    struct Model {
        friend class Loader;

        friend void sync(std::shared_ptr<Model> model);

        glm::mat4 transform{1};
        std::vector<Texture> textures{};
        std::vector<Camera> cameras;
        Indices indices{};
        VulkanBuffer vertices{};
        VulkanBuffer materials;
        VulkanBuffer textureInfos;
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

        ~Model();

        void render(VkCommandBuffer commandBuffer, VulkanPipelineLayout& layout, uint32_t meshDescriptorOffset = 0);

        void renderWithMaterial(VkCommandBuffer commandBuffer, VulkanPipelineLayout& layout, uint32_t firstSet = 0, bool blend = false);

    private:
        Condition _loaded;
        VulkanDescriptorPool* _sourceDescriptorPool{};
        BindlessDescriptor* _bindlessDescriptor{};
        uint32_t _textureBindingOffset{std::numeric_limits<uint32_t>::max()};
    };

}

