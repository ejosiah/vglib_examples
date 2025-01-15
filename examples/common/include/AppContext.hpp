#pragma once

#include "ComputePipelins.hpp"
#include "VulkanDevice.h"
#include "VulkanSwapChain.h"
#include "VulkanRenderPass.h"
#include "Prototypes.hpp"
#include "camera_base.h"
#include "Floor.hpp"
#include "atmosphere/AtmosphereDescriptor.hpp"

class AppContext {
public:
    static constexpr float SunAngularRadius = 0.004675;
    struct AtmosphereInfo {
        glm::mat4 inverse_model;
        glm::mat4 inverse_view;
        glm::mat4 inverse_projection;

        glm::vec4 camera;
        glm::vec4 earthCenter{0, - 6360 * km, 0, 1};
        glm::vec4 sunDirection{0.57735026918962576450914878050196};
        glm::vec4 whitePoint{1};
        glm::vec2 sunSize{glm::tan(SunAngularRadius), glm::cos(SunAngularRadius)};
        float exposure{10};
    };

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

    static  void addImageMemoryBarriers(VkCommandBuffer commandBuffer, const std::vector<std::reference_wrapper<VulkanImage>> &images
            ,VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            , VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    static void renderSolid(VkCommandBuffer commandBuffer, BaseCameraController& camera,  auto content, bool dynamic = false) {
        auto& solid = !dynamic ? instance._shading.solid : instance._shading.dynamic.solid;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, solid.pipeline.handle);
        camera.push(commandBuffer, solid.layout);
        bindInstanceDescriptorSets(commandBuffer, solid.layout);
        vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_BACK_BIT);
        vkCmdSetPolygonModeEXT(commandBuffer, VK_POLYGON_MODE_FILL);
        content();
    }

    static void renderFlat(VkCommandBuffer commandBuffer, BaseCameraController& camera,  auto content) {
        auto& flat = instance._shading.flat;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, flat.pipeline.handle);
        camera.push(commandBuffer, flat.layout);
        bindInstanceDescriptorSets(commandBuffer, flat.layout);
        vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_BACK_BIT);
        vkCmdSetPolygonModeEXT(commandBuffer, VK_POLYGON_MODE_FILL);
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

    static void renderAtmosphere(VkCommandBuffer commandBuffer, BaseCameraController& camera) {
        static std::array<VkDescriptorSet, 3> sets;
        sets[0] = instance._atmosphere.info.descriptorSet;
        sets[1] = instance._atmosphere.descriptor.uboDescriptorSet;
        sets[2] = instance._atmosphere.descriptor.lutDescriptorSet;

        const auto& pipeline = instance._shading.atmosphere;
        auto& info = *instance._atmosphere.info.cpu;
        info.inverse_model = glm::inverse(camera.cam().model);
        info.inverse_view = glm::inverse(camera.cam().view);
        info.inverse_projection = glm::inverse(camera.cam().proj);
        info.camera = glm::vec4{ camera.position(), 1 };

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.handle, 0, COUNT(sets), sets.data(), 0, 0);
        renderClipSpaceQuad(commandBuffer);
    }

    static void renderFloor(VkCommandBuffer commandBuffer, BaseCameraController& camera);

    static void shutdown();

    static void updateSunDirection(glm::vec3 direction);

private:
    void createDescriptorSets();

    void updateDescriptorSets();

    void init0();

    void initAtmosphere();

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
    VulkanDescriptorSetLayout _uniformDescriptorSetLayout;
    VkDescriptorSet _defaultInstanceSet{};
    VulkanBuffer _instanceTransforms;
    VulkanBuffer _clipSpaceBuffer;
    std::unique_ptr<Prototypes> _prototype{};

    static AppContext instance;

    struct {
        Pipeline solid;
        Pipeline material;
        Pipeline wireframe;
        Pipeline flat;
        Pipeline atmosphere;
        struct {
            Pipeline solid;
            Pipeline atmosphere;
        } dynamic;
    } _shading;
    Floor _floor;

    struct {
        AtmosphereDescriptor descriptor;
        struct {
            VulkanBuffer gpu;
            AtmosphereInfo* cpu{};
            VkDescriptorSet descriptorSet{};
        } info;
    } _atmosphere;

};