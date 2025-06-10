#pragma once

#include "VulkanBaseApp.h"
#include "mp4.hpp"
#include "Video.hpp"
#include "plugins/BindLessDescriptorPlugin.hpp"

class VideoPlayback : public VulkanBaseApp{
public:
    explicit VideoPlayback(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void updateDescriptorBinding(const Texture& texture);

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void beforeDeviceCreation() override;

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void loadVideo();

    void initVideoInstance();

    void createVideoSession();

    void createDpbResources();

    void createDpbOutputTexture(OutputTexture& output, const std::string& name);

    void translate(const h264::SPS& sps, StdVideoH264SequenceParameterSet& vk_sps, StdVideoH264SequenceParameterSetVui& vk_vui, StdVideoH264HrdParameters& vk_hrd);

    void translate(const h264::PPS& pps, StdVideoH264PictureParameterSet& vk_pps, StdVideoH264ScalingLists& vk_scalinglist);

    void decode(const std::shared_ptr<VideoInstance>& vInstance, VkCommandBuffer commandBuffer);

    void decode(const VideoDecodeOperation& decodeOperation, VkCommandBuffer commandBuffer);

    void renderControls(VkCommandBuffer commandBuffer);

    void initPrototypeVideoDecodeOperation();

    void getVideoCapabilities();

    void createYUVSampler();

    void createDisplayTexture();

    void createSemaphores();

    void endFrame() override;

protected:
    uint64_t VIDEO_DECODE_BITSTREAM_ALIGNMENT = 1u;
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    std::string video_playback_info;
    VideoCapabilities cb;
    std::shared_ptr<Video> video;
    std::shared_ptr<VideoInstance> video_instance;
    VideoDecodeOperation prototypeDecodeOperation{};
    VulkanSampler yuvSampler;
    VkSamplerYcbcrConversion ycbcrConversion{};
    VulkanImageView displayView;
    struct {
        Texture texture;
        VkImageSubresourceRange subresource;
    } display;  // TODO replace with VideoInstance.output_textures_used and use bindless descriptor
    VulkanDescriptorSetLayout displayDescriptorSetLayout;
    VkDescriptorSet displayDescriptorSet{};

    struct {
        VulkanSemaphore renderingFinished;
        VulkanSemaphore frameDecoded;
    } semaphores;
};