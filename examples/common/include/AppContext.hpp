#pragma once

#include "ComputePipelins.hpp"
#include "VulkanDevice.h"
#include "VulkanSwapChain.h"
#include "VulkanRenderPass.h"
#include "Prototypes.hpp"
#include "camera_base.h"
#include "Floor.hpp"

class AppContext {
public:

    static void init(VulkanDevice& device, VulkanDescriptorPool& descriptorPool, VulkanSwapChain& swapChain, VulkanRenderPass& renderPass);

    static VulkanDevice& device();

    static VulkanDescriptorPool& descriptorPool();

    static void bindInstanceDescriptorSets(VkCommandBuffer commandBuffer, VulkanPipelineLayout& layout);

    static void renderClipSpaceQuad(VkCommandBuffer commandBuffer);

    static VulkanDescriptorSetLayout & instanceSetLayout();

    static VkDescriptorSet allocateInstanceDescriptorSet();

    static void onResize(VulkanSwapChain& swapChain, VulkanRenderPass& renderPass);

    static std::vector<VkDescriptorSet> allocateDescriptorSets(const std::vector<VulkanDescriptorSetLayout>& setLayouts);

    static std::string resource(const std::string& name);

    static void renderSolid(VkCommandBuffer commandBuffer, BaseCameraController& camera,  auto content) {
        auto& solid = instance._shading.solid;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, solid.pipeline.handle);
        camera.push(commandBuffer, solid.layout);
        bindInstanceDescriptorSets(commandBuffer, solid.layout);
        vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_BACK_BIT);
        content();
    }

    static void renderWireframe(VkCommandBuffer commandBuffer, BaseCameraController& camera,  auto content, const glm::vec3& color = glm::vec3(0.2)) {
        auto& wireframe = instance._shading.wireframe;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, wireframe.pipeline.handle);
        camera.push(commandBuffer, wireframe.layout);
        vkCmdPushConstants(commandBuffer, wireframe.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(Camera), sizeof(color), &color);
        bindInstanceDescriptorSets(commandBuffer, wireframe.layout);
        vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        content();
    }

    static void renderFloor(VkCommandBuffer commandBuffer, BaseCameraController& camera);

    static void shutdown();

private:
    void createDescriptorSets();

    void updateDescriptorSets();

    void init0();

    void initPrototype();

    void createPipelines();

    void initFloor();

private:
    AppContext() = default;

    AppContext(VulkanDevice& device,
               VulkanDescriptorPool& descriptorPool,
               VulkanSwapChain& swapChain,
               VulkanRenderPass& renderPass);


    VulkanDevice* _device{};
    VulkanDescriptorPool* _descriptorPool{};
    VulkanSwapChain* _swapChain{};
    VulkanRenderPass* _renderPass{};
    VulkanDescriptorSetLayout _instanceSetLayout;
    VkDescriptorSet _defaultInstanceSet{};
    VulkanBuffer _instanceTransforms;
    VulkanBuffer _clipSpaceBuffer;
    std::unique_ptr<Prototypes> _prototype{};

    static AppContext instance;

    struct {
        Pipeline solid;
        Pipeline material;
        Pipeline wireframe;
    } _shading;
    Floor _floor;

};