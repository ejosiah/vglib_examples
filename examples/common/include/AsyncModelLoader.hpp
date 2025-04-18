#pragma once

#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "Vertex.h"
#include <plugins/BindLessDescriptorPlugin.hpp>

#include "Texture.h"
#include <assimp/scene.h>

#include <assimp/Importer.hpp>
#include <cinttypes>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <variant>
#include "RingBuffer.hpp"

namespace asyncml {

    struct Bounds {
        glm::vec3 min{MAX_FLOAT}, max{MIN_FLOAT};
    };

    enum class TextureType : int {
        DIFFUSE = 0, AMBIENT, SPECULAR, SHININESS, NORMAL, OPACITY, DISPLACEMENT, VOLUME
    };

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

    struct Draw {
        VulkanBuffer gpu;
        VkDrawIndexedIndirectCommand* cpu;
        int count;
    };

    struct Material {
        std::string name;
        glm::vec3 diffuse;
        glm::vec3 emission;
        glm::vec3 specular;
        glm::vec3 ambient;
        glm::vec3 transmittance;
        float shininess = 0;
        float ior = 0;
        float opacity = 1;
        float illum = 1;
        std::set<std::filesystem::path> textures;
    };

    struct MaterialData {
        glm::vec3 diffuse{0.8};
        float shininess = 0;

        glm::vec3 ambient{0.8};
        float ior = 0;

        glm::vec3 specular{1};
        float opacity = 1;

        glm::vec3 emission{0};
        float illum = 1;

        glm::uvec4 textures0{0};
        glm::uvec4 textures1{0};

        glm::vec3 transmittance{1};
        int padding;
    };

    struct UploadedTexture {
        std::filesystem::path path;
        Texture texture;
        TextureType type;
    };

    struct Model {
        RingBuffer<Mesh> meshes;
        RingBuffer<UploadedTexture> uploadedTextures;
        std::vector<Material> materials;
        std::vector<Texture> textures;
        VulkanBuffer vertexBuffer;
        VulkanBuffer indexBuffer;
        VulkanBuffer meshBuffer;
        VulkanBuffer materialBuffer;
        Draw draw;

        void updateDrawState(const VulkanDevice& device, BindlessDescriptor& bindlessDescriptor);

        size_t numMeshes() const { return meshBuffer.sizeAs<MeshData>(); }

        size_t numMaterials() const { return materialBuffer.sizeAs<MaterialData>(); }
    };

    struct Voxel {
        glm::vec3 position;
        float value;
    };

    struct VdbVolume {
        float invMaxDensity{0};
        std::vector<Voxel> voxels;
        Bounds bounds{glm::vec3(0), glm::vec3(0)};
    };

    struct Frame {
        VdbVolume volume;
        int index{0};
        int elapsedMS{};
        int durationMS{std::numeric_limits<int>::max()};
    };

    struct UploadedVolume {
        std::filesystem::path path;
        VdbVolume volume;
    };

    struct Volume {
        RingBuffer<UploadedVolume> UploadedVolumes;
        std::vector<Frame> frames;
        int framePeriod = 100;
        VulkanBuffer staging;
        int currentFrame{0};
        int numFrames{0};
        Bounds bounds{};
        glm::mat4 transform{1};
        glm::vec3 offset{0};
        float scale{0.01};
        std::atomic_bool initialized{false};
        bool ready{false};
        uint32_t textureBindingIndex{10};
        float invMaxDensity{};
        Texture texture{};
        std::atomic_bool nextFrame{false};

        void checkLoadState(BindlessDescriptor& bindlessDescriptor);

        void update(float dt);

        void updateTransform();

        Bounds scaledBounds() const {
            auto scaled = bounds;
            scaled.min *= scale;
            scaled.max *= scale;
            return scaled;
        }

        void advanceFrame(VulkanDevice& device);

    };

    struct PendingModel {
        std::shared_ptr<Model> model;
        const aiScene* scene;
        std::filesystem::path path;
        float unit{1};
    };

    struct PendingVolume {
        std::shared_ptr<Volume> volume;
        std::vector<std::filesystem::path> paths;
    };

    struct TextureUploadRequest {
        std::filesystem::path path;
        std::shared_ptr<Model> model;
        TextureType type;
    };

    struct VolumeUploadRequest {
        std::filesystem::path path;
        std::shared_ptr<Volume> volume;
    };

    struct MaterialTexture {
        std::filesystem::path path;
        int materialId;
    };

    class Loader {
    public:
        Loader() = default;

        explicit Loader(VulkanDevice* device, size_t reserve = 32);

        void start();

        std::shared_ptr<Model> load(const std::filesystem::path& path, float unit = meter);

        std::shared_ptr<Volume> loadVolume(const std::filesystem::path& path);

        void execLoop();

        void stop();

    private:

         std::set<std::filesystem::path> extractTextures(const aiMaterial& material, const std::shared_ptr<Model>& model, const std::filesystem::path& path);

         void uploadMeshes();

         void processVolumes();

         void uploadVolumes();

         void uploadTextures();

         void createTexture(TextureUploadRequest& request);

         Material extractMaterial(const aiMaterial* aiMaterial);

         Bounds volumeBounds(const std::filesystem::path& path);

    private:
        VulkanDevice* _device{};
        RingBuffer<PendingModel> _pendingModels;
        RingBuffer<PendingVolume> _pendingVolumes;
        RingBuffer<TextureUploadRequest> _pendingUploads;
        RingBuffer<VolumeUploadRequest> _pendingVolumeUploads;
        VulkanFence _fence;
        std::atomic_bool _running{false};
        std::thread _thread;
        VulkanBuffer _stagingBuffer;
        Assimp::Importer _importer;
        std::set<std::filesystem::path> _uploadedTextures;
        std::condition_variable _loadRequest;
        std::mutex _mutex;
    };

}