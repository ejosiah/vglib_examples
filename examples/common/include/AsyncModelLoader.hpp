#pragma once

#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "Vertex.h"
#include "Texture.h"

#include <assimp/scene.h>
#include <assimp/Importer.hpp>

#include <cinttypes>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>

namespace asyncml {

    template<typename T>
    class RingBuffer {
    public:
        RingBuffer() = default;

        explicit RingBuffer(size_t capacity)
        : _data(capacity)
        , _capacity{capacity}
        {}

        constexpr void push(const T& entry) {
            ensureCapacity();
            _data[++_writeIndex % _capacity] = entry;
        }

        constexpr void push(T&& entry) {
            ensureCapacity();
            _data[++_writeIndex % _capacity] = std::move(entry);
        }

        T pop() {
            ensureCapacity();
            return std::move(_data[_readIndex++ % _capacity]);
        }

        [[nodiscard]]
        bool empty() const {
            return _writeIndex < _readIndex;
        }

        [[nodiscard]]
        bool full() const {
            auto size = (_writeIndex - _readIndex) + 1;
            return size == _capacity;
        }

        size_t size() const {
            return (_writeIndex - _readIndex) + 1;
        }

        void ensureCapacity() {
            assert(_capacity > 0);
        }

        void reset(size_t newCapacity) {
            _data.clear();
            _data.resize(newCapacity);
            _capacity = newCapacity;
            _writeIndex = -1;
            _readIndex = 0;
        }

    private:
        std::vector<T> _data;
        size_t _capacity{0};
        std::atomic_int64_t _writeIndex{-1};
        std::atomic_int64_t _readIndex{0};
    };


    struct Mesh{
        uint32_t meshId{};
        uint32_t indexCount{};
        uint32_t instanceCount{};
        uint32_t firstIndex{};
        uint32_t  vertexOffset{};
        uint32_t firstInstance{};

        glm::vec3 diffuse = glm::vec3(0.6f);
        glm::vec3 ambient = glm::vec3(0.6f);
        glm::vec3 specular = glm::vec3(1);
        glm::vec3 emission = glm::vec3(0);
        glm::vec3 transmittance = glm::vec3(0);
        float shininess = 0;
        float ior = 0;
        float opacity = 1;
        float illum = 1;
    };

    struct MeshData {
        glm::mat4 model{1};
        glm::mat4 ModelInverse{1};
        glm::vec3 diffuse;
        float shininess = 0;

        glm::vec3 ambient;
        float ior = 0;

        glm::vec3 specular;
        float opacity = 1;

        glm::vec3 emission;
        float illum = 1;

        glm::uvec4 textures0{0};
        glm::uvec4 textures1{0};

        glm::vec3 transmittance;
        int padding;
    };

    struct Draw {
        VulkanBuffer gpu;
        VkDrawIndexedIndirectCommand* cpu;
        int count;
    };

    struct Material {
        Texture texture;
        int id;
        uint32_t meshId;
    };

    struct Model {
        RingBuffer<Mesh> meshes;
        RingBuffer<Material> materials;
        std::vector<Texture> textures;
        VulkanBuffer vertexBuffer;
        VulkanBuffer indexBuffer;
        VulkanBuffer meshBuffer;
        std::map<uint32_t, uint32_t> meshDrawIds{};
        Draw draw;

        void updateDrawState(const VulkanDevice& device, VkDescriptorSet bindlessDescriptor);
    };

    struct Pending {
        std::shared_ptr<Model> model;
        const aiScene* scene;
        std::filesystem::path path;
        float unit{1};
    };

    struct TextureUploadRequest {
        std::filesystem::path path;
        std::shared_ptr<Model> model;
        int id;
        uint32_t meshId;
    };


    class Loader {
    public:
        Loader() = default;

        explicit Loader(VulkanDevice* device, size_t reserve = 32);

        void start();

        std::shared_ptr<Model> load(const std::filesystem::path& path, float unit = meter);

        void execLoop();

        void stop();

    private:
         void extractTextures(const Pending& pending, const aiMesh* mesh, uint32_t meshId);

         void uploadMeshes();

         void uploadTextures();

         void createTexture(const TextureUploadRequest& request);

         void extractMaterial(const aiMaterial* aiMaterial, Mesh& mesh);

    private:
        VulkanDevice* _device{};
        RingBuffer<Pending> _pending;
        RingBuffer<TextureUploadRequest> _pendingUploads;
        VulkanFence _fence;
        std::atomic_bool _running{false};
        std::thread _thread;
        VulkanBuffer _stagingBuffer;
        Assimp::Importer _importer;
    };

}