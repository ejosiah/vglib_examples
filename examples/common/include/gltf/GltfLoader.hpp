#pragma once

#include "gltf.hpp"

#include <VulkanRAII.h>

#include <memory>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace gltf {

    class Loader {
    public:
        Loader() = default;

        Loader(VulkanDevice* device, VulkanDescriptorPool* descriptorPool, BindlessDescriptor* bindlessDescriptor, size_t reserve = 32);

        void initPlaceHolders();

        void start();

        std::shared_ptr<Model> load(const std::filesystem::path& path);

        void execLoop();

        void stop();

        const VulkanDescriptorSetLayout & descriptorSetLayout() const {
            return _descriptorSetLayout;
        }

    private:
        void uploadMeshes();

        void uploadTextures();

        void uploadMaterials();

        void createDescriptorSetLayout();

    private:
        VulkanDevice* _device{};
        VulkanDescriptorPool* _descriptorPool{};
        RingBuffer<std::shared_ptr<PendingModel>> _pendingModels;
        RingBuffer<TextureUploadRequest> _texturesUploads;

        std::thread _thread;
        VulkanFence _fence;
        std::atomic_bool _running{};
        VulkanBuffer _stagingBuffer;
        std::condition_variable _loadRequest;
        std::mutex _mutex;
        Texture _placeHolderTexture;
        Texture _placeHolderNormalTexture;
        BindlessDescriptor* _bindlessDescriptor{};
        VulkanDescriptorSetLayout _descriptorSetLayout;
    };


}