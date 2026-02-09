#define TINYGLTF_IMPLEMENTATION
#include "gltf/gltf.hpp"

namespace gltf {

    void sync(std::shared_ptr<Model> model) {
        model->_loaded.wait();
    }

    Model::~Model() {
        if(_sourceDescriptorPool) {
            std::vector<VkDescriptorSet> sets{
                    meshDescriptorSet.u16.handle,
                    meshDescriptorSet.u32.handle,
                    materialDescriptorSet
            };
            _sourceDescriptorPool->free(sets);
        }
    }

    void Model::render(VkCommandBuffer commandBuffer, VulkanPipelineLayout& layout, uint32_t meshDescriptorOffset) {
        static VkDeviceSize offset = 0;

        auto blendingEnabled = VK_TRUE;
        const auto firstSet = meshDescriptorOffset;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertices, &offset);

//        vkCmdSetColorBlendEnableEXT(commandBuffer, 0, 1, &blendingEnabled);

        if (draw.u16.count > 0) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout.handle, firstSet, 1, &meshDescriptorSet.u16.handle, 0, VK_NULL_HANDLE);
            vkCmdBindIndexBuffer(commandBuffer, indices.u16.handle, 0, VK_INDEX_TYPE_UINT16);
            vkCmdDrawIndexedIndirect(commandBuffer, draw.u16.handle, 0, draw.u16.count, sizeof(VkDrawIndexedIndirectCommand));
        }

        if (draw.u32.count > 0) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout.handle, firstSet, 1, &meshDescriptorSet.u32.handle, 0, VK_NULL_HANDLE);
            vkCmdBindIndexBuffer(commandBuffer, indices.u32.handle, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexedIndirect(commandBuffer, draw.u32.handle, 0, draw.u32.count, sizeof(VkDrawIndexedIndirectCommand));
        }

        if (draw.u8.count > 0) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout.handle, firstSet, 1, &meshDescriptorSet.u8.handle, 0, VK_NULL_HANDLE);
            vkCmdBindIndexBuffer(commandBuffer, indices.u8.handle, 0, VK_INDEX_TYPE_UINT8_EXT);
            vkCmdDrawIndexedIndirect(commandBuffer, draw.u8.handle, 0, draw.u8.count, sizeof(VkDrawIndexedIndirectCommand));
        }
    }

    void Model::renderWithMaterial(VkCommandBuffer commandBuffer, const VulkanPipelineLayout &layout, uint32_t firstSet, bool blend) {
        static std::vector<VkDescriptorSet> sets(4);
        sets[0] = meshDescriptorSet.u16.handle;
        sets[1] = materialDescriptorSet;
        sets[2] = _bindlessDescriptor->descriptorSet;
        sets[3] = rtxDescriptorSet;

        auto blendingEnabled = static_cast<VkBool32>(blend);
        vkCmdSetColorBlendEnableEXT(commandBuffer, 0, 1, &blendingEnabled);
    
        VkDeviceSize offset = 0;
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout.handle, firstSet, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertices, &offset);
        
        vkCmdBindIndexBuffer(commandBuffer, indices.u16.handle, 0, VK_INDEX_TYPE_UINT16);
        vkCmdDrawIndexedIndirect(commandBuffer, draw.u16.handle, 0, draw.u16.count, sizeof(VkDrawIndexedIndirectCommand));
        
        sets[0] = meshDescriptorSet.u32.handle;
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout.handle, firstSet, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdBindIndexBuffer(commandBuffer, indices.u32.handle, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexedIndirect(commandBuffer, draw.u32.handle, 0, draw.u32.count, sizeof(VkDrawIndexedIndirectCommand));
        
        sets[0] = meshDescriptorSet.u8.handle;
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout.handle, firstSet, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdBindIndexBuffer(commandBuffer, indices.u8.handle, 0, VK_INDEX_TYPE_UINT8_EXT);
        vkCmdDrawIndexedIndirect(commandBuffer, draw.u8.handle, 0, draw.u8.count, sizeof(VkDrawIndexedIndirectCommand));

    }
}