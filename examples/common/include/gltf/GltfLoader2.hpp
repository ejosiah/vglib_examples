#pragma once

#include <tiny_gltf.h>
#include "gltf2.hpp"
#include "RingBuffer.hpp"
#include "Condition.hpp"
#include "ObjectPool.hpp"
#include <variant>

namespace gltf2 {

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

        float metalness{1};
        float roughness{1};
        int alphaMode{AlphaMode::OPAQUE_};
        int doubleSided{0};

        std::array<int, 8> textures{};
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
        struct {
            std::atomic_uint32_t u16{};
            std::atomic_uint32_t u32{};
        } drawOffset;
    };

    struct MeshUploadTask {
        std::shared_ptr<PendingModel> pending;
        tinygltf::Mesh mesh;
        uint32_t meshId{};
        std::vector<Mesh> primitives;
    };

    struct TextureUploadTask {
        std::shared_ptr<PendingModel> pending;
        tinygltf::Texture texture;
        uint32_t bindingId{};
        Texture* gpuTexture;
    };

    struct MaterialUploadTask {
        std::shared_ptr<PendingModel> pending;
        tinygltf::Material material;
    };

    struct InstanceUploadTask {
        std::shared_ptr<PendingModel> pending;
        std::vector<Mesh> primitives;
        struct {
            uint32_t u16{};
            uint32_t u32{};
        } drawCounts;
    };

    struct StopWorkerTask {};

    using Task = std::variant<MeshUploadTask, TextureUploadTask, MaterialUploadTask, InstanceUploadTask, StopWorkerTask>;

    struct SecondaryCommandBuffer {
        std::vector<VkCommandBuffer> commandBuffers;
        Task task;
    };

    struct CompletedTask {
        Task task;
    };

    using BufferMemoryBarrierPool = ObjectPool<VkBufferMemoryBarrier>;
    using ImageMemoryBarrierPool = ObjectPool<VkImageMemoryBarrier>;
    using BufferCopyPool = ObjectPool<VkBufferCopy>;
    using BufferImageCopyPool = ObjectPool<VkBufferImageCopy>;

    class Loader {
    public:
        Loader() = default;

        Loader(VulkanDevice* device, VulkanDescriptorPool* descriptorPool, BindlessDescriptor* bindlessDescriptor, size_t numWorkers = 1);

        void start();

        std::shared_ptr<Model> load(const std::filesystem::path& path);

        void stop();

    private:
        void coordinatorLoop();

        void execute(const std::vector<VkCommandBuffer>& commandBuffers);

        void workerLoop(int id);

        VkCommandBuffer processTask(Task& task, int workerId);

         void process(VkCommandBuffer, MeshUploadTask* meshUpload, int workerId);

         void process(VkCommandBuffer, TextureUploadTask* textureUpload, int workerId);

         void process(VkCommandBuffer, MaterialUploadTask* materialUpload, int workerId);

         void process(VkCommandBuffer, InstanceUploadTask* instanceUpload, int workerId);

         void transferMeshInstance(VkCommandBuffer commandBuffer, VulkanBuffer& drawCmdSrc, VulkanBuffer& drawCmdDst, VulkanBuffer& meshDataSrc, VulkanBuffer& meshDataDst, uint32_t drawOffset, int workerId);

         void onComplete(Task& task);

         void onComplete(MeshUploadTask* meshUpload);

         void onComplete(InstanceUploadTask* instanceUpload);

         void onComplete(TextureUploadTask* textureUpload);

        void createDescriptorSetLayout();

        void initPlaceHolders();

        void initCommandPools();

        BufferRegion allocate(VkDeviceSize size);

        void release(BufferRegion region);

    private:
        VulkanDevice* _device{};
        VulkanDescriptorPool* _descriptorPool{};
        BindlessDescriptor* _bindlessDescriptor{};
        RingBuffer<std::shared_ptr<PendingModel>> _pendingModels;
        SingleWriterManyReadersQueue<Task> _workerQueue;
        ManyWritersSingleReaderQueue<SecondaryCommandBuffer> _commandBufferQueue;
        size_t _workerCount{};
        std::vector<VulkanCommandPool> _workerCommandPools;
        std::vector<VkCommandBuffer> _commandBuffers;

        struct {
            VulkanBuffer buffer;
            VkDeviceSize offset{};
        } _staging;

        struct {
            std::atomic_uint32_t vertexOffset{};
            std::atomic_uint32_t firstIndex16{};
            std::atomic_uint32_t firstIndex32{};
        } _offsets;

        std::thread _coordinator;
        std::vector<std::thread> _workers;
        VulkanFence _fence;
        Condition _modelLoadPending{};
        Condition _taskPending{};
        std::atomic_bool _running{};
        Texture _placeHolderTexture;
        Texture _placeHolderNormalTexture;
        VulkanDescriptorSetLayout _descriptorSetLayout;
        std::vector<BufferMemoryBarrierPool> _barrierObjectPools;
        std::vector<ImageMemoryBarrierPool> _imageMemoryBarrierObjectPools;
        std::vector<BufferCopyPool> _bufferCopyPool;
        std::vector<BufferImageCopyPool> _bufferImageCopyPool;
        uint32_t _modelId{};

        static constexpr uint32_t MegaBytes =  1024 * 1024;
        static constexpr VkDeviceSize stagingBufferSize = 128 * MegaBytes;
        std::vector<VulkanBuffer> _stagingRefs; // FIXME remove this indefinite buffer lifetime hack
    };

}