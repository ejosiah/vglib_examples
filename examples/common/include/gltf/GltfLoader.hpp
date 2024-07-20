#pragma once

#include <tiny_gltf.h>
#include "gltf.hpp"
#include "RingBuffer.hpp"
#include "Condition.hpp"
#include "ObjectPool.hpp"
#include "StagingBuffer.hpp"

#include <variant>

namespace gltf {

    struct Mesh{
        uint32_t meshId{};
        uint32_t indexCount{};
        uint32_t instanceCount{};
        uint32_t firstIndex{};
        uint32_t  vertexOffset{};
        uint32_t firstInstance{};
        uint32_t materialId{};
        ComponentType indexType{VK_INDEX_TYPE_UINT16};
        std::string name;
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

        float metalness{1};
        float roughness{1};
        int alphaMode{AlphaMode::OPAQUE_};
        int doubleSided{0};

        float transmission{0};
        float ior{1.5};
        float thickness{0};
        float attenuationDistance{0};

        glm::vec3 attenuationColor{1};
        float dispersion{0};

        float emissiveStrength{1.0};
        float clearCoatFactor{0};
        float clearCoatRoughnessFactor{0};
        int textureInfoOffset{0};
    };

    struct TextureInfo {
        glm::vec2 offset{0};
        glm::vec2 scale{1};
        float tScale{1};
        int index{-1};
        int texCoord{0};
        float rotation{0};
    };

    struct LightInstance {
        glm::mat4 model{1};
        glm::mat4 ModelInverse{1};
        int lightId{0};
        char padding[12]{};
    };

    struct PendingModel {
        VulkanDevice* device;
        BindlessDescriptor* bindlessDescriptor{};
        std::unique_ptr<tinygltf::Model> gltf{};
        std::shared_ptr<Model> model;
        std::vector<glm::mat4> transforms;
        int textureBindingOffset{};
        std::vector<Mesh> meshes;
        std::atomic_uint32_t meshID{};
        std::vector<Texture> textures;
        std::atomic_uint32_t textureId;
        uint32_t numLights{};
        struct {
            std::atomic_uint32_t u8{};
            std::atomic_uint32_t u16{};
            std::atomic_uint32_t u32{};
        } drawOffset;
        struct {
            std::vector<uint32_t> vertex;
            std::vector<uint32_t> u8;
            std::vector<uint32_t> u16;
            std::vector<uint32_t> u32;
        } offsets;
    };

    struct MeshUploadTask {
        std::shared_ptr<PendingModel> pending;
        tinygltf::Mesh mesh;
        uint32_t meshId{};
        std::vector<Mesh> primitives;
    };

    struct GltfTextureUploadTask {
        std::shared_ptr<PendingModel> pending;
        tinygltf::Texture texture;
        uint32_t bindingId{};
        uint32_t textureId;
    };

    struct TextureUploadStatus{
        friend class Loader;

        Texture* texture{};
        std::atomic_bool success{};

        void sync() {
            _ready.wait();
        }

    private:
        Condition _ready{};
    };

    struct TextureUploadTask {
        std::string path;
        int desiredChannels{};
        std::shared_ptr<TextureUploadStatus> status;
    };

    struct MaterialUploadTask {
        std::shared_ptr<PendingModel> pending;
        tinygltf::Material material;
        uint32_t materialId;
    };

    struct InstanceUploadTask {
        std::shared_ptr<PendingModel> pending;
        std::vector<Mesh> primitives;
        struct {
            uint32_t u8{};
            uint32_t u16{};
            uint32_t u32{};
        } drawCounts;
    };

    struct LightUploadTask {
        std::shared_ptr<PendingModel> pending;
        uint32_t LightId{};
    };

    struct LightInstanceUploadTask {
        std::shared_ptr<PendingModel> pending;
        tinygltf::Node node;
        uint32_t nodeId{};
        uint32_t instanceId{};
    };

    struct StopWorkerTask {};

    using Task = std::variant<MeshUploadTask, GltfTextureUploadTask, MaterialUploadTask, InstanceUploadTask, TextureUploadTask
                            , LightUploadTask, LightInstanceUploadTask, StopWorkerTask>;

    struct SecondaryCommandBuffer {
        std::vector<VkCommandBuffer> commandBuffers;
        Task task;
    };

    struct CompletedTask {
        Task task;
    };

    using BufferMemoryBarrierPool = CyclicObjectPool<VkBufferMemoryBarrier>;
    using ImageMemoryBarrierPool = CyclicObjectPool<VkImageMemoryBarrier>;
    using BufferCopyPool = CyclicObjectPool<VkBufferCopy>;
    using BufferImageCopyPool = CyclicObjectPool<VkBufferImageCopy>;

    struct WorkerCommandPools{
        VulkanCommandPool transferPool;
        VulkanCommandPool graphicsPool;
        VulkanCommandPool computePool;
    };

    class Loader {
    public:
        Loader() = default;

        Loader(VulkanDevice* device, VulkanDescriptorPool* descriptorPool, BindlessDescriptor* bindlessDescriptor, size_t numWorkers = 1);

        void start();

        std::shared_ptr<Model> loadGltf(const std::filesystem::path& path);

        std::shared_ptr<TextureUploadStatus> loadTexture(const std::filesystem::path& path, Texture& texture);

        void stop();

        void commandBufferBatchSize(size_t size);

        VulkanDescriptorSetLayout descriptorSetLayout() const;

        VulkanDescriptorSetLayout materialDescriptorSetLayout() const;

        void finalizeGltfTextureTransfer();

        void finalizeRegularTextureTransfer();

        std::shared_ptr<Model> dummyModel();


    private:
        void coordinatorLoop();

        void execute(const std::vector<VkCommandBuffer>& commandBuffers);

        void workerLoop(int id);

        VkCommandBuffer processTask(Task& task, int workerId);

         void process(VkCommandBuffer, MeshUploadTask* meshUpload, int workerId);

         void process(VkCommandBuffer, GltfTextureUploadTask* textureUpload, int workerId);

         void process(VkCommandBuffer, MaterialUploadTask* materialUpload, int workerId);

         void process(VkCommandBuffer, InstanceUploadTask* instanceUpload, int workerId);

         void process(VkCommandBuffer commandBuffer, TextureUploadTask* textureUpload, int workerId);

         void process(VkCommandBuffer commandBuffer, LightUploadTask* lightUpload, int workerId);

         void process(VkCommandBuffer commandBuffer, LightInstanceUploadTask* lightInstanceUpload, int workerId);

         void transferMeshInstance(VkCommandBuffer commandBuffer, BufferRegion& drawCmdSrc, VulkanBuffer& drawCmdDst, BufferRegion& meshDataSrc, VulkanBuffer& meshDataDst, uint32_t drawOffset, int workerId);

         void onComplete(Task& task);

         void onComplete(MeshUploadTask* meshUpload);

         void onComplete(InstanceUploadTask* instanceUpload);

         void onComplete(GltfTextureUploadTask* textureUpload);

         void onComplete(MaterialUploadTask* materialUpload);

         void onComplete(TextureUploadTask* textureUpload);

         void onComplete(LightUploadTask* lightUpload);

         void onComplete(LightInstanceUploadTask* lightInstanceUpload);

         void handleError(Task& task, const std::exception& exception);

         void handleError(TextureUploadTask* textureUpload, const std::exception& exception);

        void createDescriptorSetLayout();

        void initPlaceHolders();

        void initCommandPools();

        TextureInfo extract(const tinygltf::TextureInfo& info, int offset);

        TextureInfo extract(const tinygltf::NormalTextureInfo& info, int offset);

        TextureInfo extract(const tinygltf::OcclusionTextureInfo& info, int offset);

        VulkanSampler createSampler(const tinygltf::Model& model, int sampler, uint32_t mipLevels);

        friend bool isFloat(VkFormat format);

    private:
        VulkanDevice* _device{};
        VulkanDescriptorPool* _descriptorPool{};
        BindlessDescriptor* _bindlessDescriptor{};
        RingBuffer<std::shared_ptr<PendingModel>> _pendingModels;
        RingBuffer<TextureUploadTask> _pendingTextureUploads;
        SingleWriterManyReadersQueue<Task> _workerQueue;
        ManyWritersSingleReaderQueue<SecondaryCommandBuffer> _commandBufferQueue;
        size_t _workerCount{};
        std::vector<WorkerCommandPools> _workerCommandPools;
        std::vector<VkCommandBuffer> _commandBuffers;
        VulkanCommandPool _graphicsCommandPool;

        std::vector<StagingBuffer> _stagingBuffers;
        RingBuffer<GltfTextureUploadTask> _readyTextures;
        RingBuffer<TextureUploadTask> _uploadedTextures;

        std::thread _coordinator;
        std::vector<std::thread> _workers;
        VulkanFence _fence;
        Condition _coordinatorWorkAvailable{};
        Condition _taskPending{};
        std::atomic_bool _running{};
        Texture _placeHolderTexture;
        Texture _placeHolderNormalTexture;
        VulkanDescriptorSetLayout _descriptorSetLayout;
        VulkanDescriptorSetLayout _materialDescriptorSetLayout;
        std::vector<BufferMemoryBarrierPool> _barrierObjectPools;
        std::vector<ImageMemoryBarrierPool> _imageMemoryBarrierObjectPools;
        std::vector<BufferCopyPool> _bufferCopyPool;
        std::vector<BufferImageCopyPool> _bufferImageCopyPool;
        uint32_t _modelId{};
        uint32_t _commandBufferBatchSize{1};
        static constexpr uint32_t MegaBytes =  1024 * 1024;
        static constexpr VkDeviceSize stagingBufferSize = 1024 * MegaBytes;
        static const std::map<int, VkFormat> channelFormatMap;
        static const std::vector<VkFormat> floatFormats;
    };

}