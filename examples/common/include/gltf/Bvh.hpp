#pragma once


#include "gltf.hpp"
#include "VulkanBuffer.h"
#include "VulkanModel.h"
#include "VulkanDevice.h"

namespace gltf::bvh {

    enum class IndexType { U8, U16, U32 };

    struct AccelerationStructure {
        VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
        VkDeviceAddress deviceAddress = 0;
        VulkanBuffer buffer;
        VkBuildAccelerationStructureFlagsKHR flags = 0;
    };

    struct ScratchBuffer {
        VulkanBuffer handle;
        VkDeviceAddress address = 0;
    };

    using Blas = AccelerationStructure;
    using Tlas = AccelerationStructure;

    class Bvh {
    public:
        Bvh() = default;

        Bvh(VulkanDevice& device, VulkanDescriptorPool& descriptorPool, std::shared_ptr<gltf::Model> model);

        ~Bvh();

        void build();

        void update();

        static VulkanDescriptorSetLayout rtxDescriptorSetLayout;

        static void createDescriptorSetLayout(VulkanDevice& device);

    private:
        void createBlas();

        void createTlas();

        std::vector<VkAccelerationStructureInstanceKHR> createInstances(const VulkanBuffer &staging, VulkanBuffer& instanceBuffer,
                                                                        std::map<int, int>& isntanceBlasMap, size_t blasOffset, uint32_t customIndex);

        std::tuple<std::map<int, int>, std::vector<Blas>> createBlas(VulkanBuffer& staging, DrawGroup& draw,
                                                                     VulkanBuffer& vertices,
                                                                     VulkanBuffer& indexes, IndexType indexType);

        VulkanDevice& device();

        void createDescriptorSet();

    private:
        VulkanDevice* m_device;
        VulkanDescriptorPool* m_descriptorPool;
        std::shared_ptr<Model> m_model;
        std::vector<Blas> m_blas;
        Tlas m_tlas;
        struct {
            size_t u8{};
            size_t u16{};
            size_t u32{};
        } m_blasOffset;

        struct {
            std::map<int, int> u8;
            std::map<int, int> u16;
            std::map<int, int> u32;
        } m_instanceMeshMap;
    };
}
