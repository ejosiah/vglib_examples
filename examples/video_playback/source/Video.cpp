#include "common.h"
#include "Video.hpp"
#include "Barrier.hpp"

void VideoInstance::seek(float time) {
    if (!video) return;
    const auto &frames = video->frame_infos;

    auto targetIFrame = 0;
    for (auto i = 0; i < frames.size(); ++i) {
        auto &frame = frames[i];
        if (frame.type == VideoFrameType::Intra) {
            targetIFrame = i;
        }
        if (frame.timestamp_seconds > time)
            break;
    }

    if (current_decode_frame != targetIFrame) {
        current_decode_frame = to<int>(targetIFrame);
        current_time = video->frame_infos[targetIFrame].timestamp_seconds;
        target_display_order = video->frame_infos[targetIFrame].display_order;
        flags |= Flags::DecoderReset;
        flags &= ~Flags::FirstFrameDecoded;
        output_textures_free.clear();
        output_textures_used.clear();
        output_textures_resolve_request.clear();
    }
}

void VideoInstance::update(float dt) {

    if (has_flag(flags, Flags::Looped) && current_time > video->duration_seconds) {
        current_time = 0;
        current_decode_frame = 0;
    }
    const auto numFrames = video->frame_infos.size();
    for (auto i = 0; i < numFrames; ++i) {
        const auto &frame = video->frame_infos[i];
        const auto frameStartTime = frame.timestamp_seconds;
        const auto frameEndTime = frameStartTime + frame.duration_seconds;

        if (frameStartTime <= current_time && frameEndTime > current_time) {
            target_display_order = frame.display_order;
            break;
        }
    }

     updateDisplayOrderOutput();

    if (has_flag(flags, Flags::Playing)) {
        current_time += dt;
        if(!has_flag(flags, Flags::Looped)) {
            current_time = std::min(current_time, video->duration_seconds);
        }
    }
}

void VideoInstance::updateDisplayOrderOutput() {
    // Check if current output texture can be replaced by a newer one:
    if (output.display_order != target_display_order) {
        for (size_t i = 0; i < output_textures_used.size(); ++i) {
            if (output_textures_used[i].display_order == target_display_order) {
                if (output.display.texture.isValid()) {
                    // Free current output texture:
                    output_textures_free.push_back(std::move(output));
                    output = {};
                }

                // Take this used texture as current output:
                output = std::move(output_textures_used[i]);

                // Remove this used texture:
                std::swap(output_textures_used[i], output_textures_used.back());
                output_textures_used.pop_back();
                break;
            }
        }
    }

    // Check if any used textures can be freed because their display order is outdated:
    for (size_t i = 0; i < output_textures_used.size(); ++i) {
        if (output_textures_used[i].display_order < target_display_order) {
            // Remove this used texture:
            output_textures_free.push_back(std::move(output_textures_used[i]));
            std::swap(output_textures_used[i], output_textures_used.back());
            output_textures_used.pop_back();
        }
    }
}

void VideoInstance::resolveToDisplay(VkCommandBuffer commandBuffer, VulkanDevice& device) {
    if(!video) return;

    if(!has_flag(flags, Flags::NeedsResolve)) return;

//    flags &= ~Flags::NeedsResolve;
    std::vector<std::string> released{};
    for(auto resolveId : output_textures_resolve_request) {
        auto& out = output_textures_used[resolveId];

        Barriers::push(out.src.texture->image, out.src.subresource_luminance, VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        Barriers::push(out.src.texture->image, out.src.subresource_chrominance, VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        Barriers::flush(commandBuffer);

        std::vector<VkImageCopy> regions {
                {
                        .srcSubresource = {
                                .aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT,
                                .baseArrayLayer = out.src.subresource_luminance.baseArrayLayer,
                                .layerCount = 1,
                        },
                        .srcOffset = {0, 0, 0},
                        .dstSubresource = {
                                .aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT,
                                .baseArrayLayer = 0,
                                .layerCount = 1
                        },
                        .dstOffset = {0, 0, 0},
                        .extent = { video->width, video->height, 1 }
                },
                {
                        .srcSubresource = {
                                .aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT,
                                .baseArrayLayer = out.src.subresource_chrominance.baseArrayLayer,
                                .layerCount = 1,
                        },
                        .srcOffset = {0, 0, 0},
                        .dstSubresource = {
                                .aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT,
                                .baseArrayLayer = 0,
                                .layerCount = 1
                        },
                        .dstOffset = {0, 0, 0},
                        .extent = { video->width/2, video->height/2, 1 }
                },
        };

        vkCmdCopyImage(commandBuffer, out.src.texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                , out.display.texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, COUNT(regions), regions.data());

        Barriers::push(out.src.texture->image, out.src.subresource_luminance, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,
                       VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR);

        Barriers::push(out.src.texture->image, out.src.subresource_chrominance, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,
                       VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR);

        Barriers::release(out.display.texture.image, out.display.subresource_luminance, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, *device.queueFamilyIndex.video_decode, *device.queueFamilyIndex.graphics);

        Barriers::release(out.display.texture.image, out.display.subresource_chrominance, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, *device.queueFamilyIndex.video_decode, *device.queueFamilyIndex.graphics);

        Barriers::flush(commandBuffer);
        released.push_back(out.name);
    }
//    output_textures_resolve_request.clear();
}

bool VideoInstance::isDecodingRequired(VulkanDevice &device) {
    // TODO check which profile is supported
    if (!video) return false;
    if (current_decode_frame >= video->frame_infos.size()) return false;
    if (!has_flag(flags, Flags::FirstFrameDecoded)) return true;

    return findNextDisplayableOutput() == nullptr;
}

OutputTexture* VideoInstance::findNextDisplayableOutput() {
    auto itr = std::find_if(output_textures_used.begin(), output_textures_used.end(), [this](const auto& out){
        return out.display_order == target_display_order + 1;
    });
    return itr != output_textures_used.end() ? &(*itr) : nullptr;
}

uint32_t VideoInstance::width() const {
    return video->width;
}

uint32_t VideoInstance::height() const {
    return video->height;
}

void Video::getCapabilities(VulkanDevice &device, VideoCapabilities &capabilities) {
    auto &cb = capabilities;

    cb.h264DecodeCapabilities = VkVideoDecodeH264CapabilitiesKHR{
            .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR
    };
    cb.decodeCapabilities = VkVideoDecodeCapabilitiesKHR{
            .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR,
            .pNext = &cb.h264DecodeCapabilities
    };
    cb.h264ProfileInfo = VkVideoDecodeH264ProfileInfoKHR{
            .sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR,
            .stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_BASELINE,
            .pictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR
    };
    cb.profile = VkVideoProfileInfoKHR{
            .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
            .pNext = &cb.h264ProfileInfo,
            .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR,
            .chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
            .lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
            .chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR
    };
    cb.capabilities = VkVideoCapabilitiesKHR{
            .sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR,
            .pNext = &cb.decodeCapabilities
    };

    vkGetPhysicalDeviceVideoCapabilitiesKHR(device.physicalDevice, &cb.profile, &cb.capabilities);

    cb.profiles = VkVideoProfileListInfoKHR{
            .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR,
            .profileCount = 1,
            .pProfiles = &cb.profile
    };

    uint32_t count;
    VkPhysicalDeviceVideoFormatInfoKHR formatInfo{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR,
            .pNext = &cb.profiles,
            .imageUsage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR
    };
    ERR_GUARD_VULKAN(vkGetPhysicalDeviceVideoFormatPropertiesKHR(device.physicalDevice, &formatInfo, &count, nullptr));

    cb.formats = std::vector<VkVideoFormatPropertiesKHR>(count,
                                                         {.sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR});
    ERR_GUARD_VULKAN(
            vkGetPhysicalDeviceVideoFormatPropertiesKHR(device.physicalDevice, &formatInfo, &count, cb.formats.data()));
}

uint64_t Video::getBitStringAlignment(VulkanDevice &device) {
    VideoCapabilities cb{};
    getCapabilities(device, cb);
    return cb.capabilities.minBitstreamBufferOffsetAlignment;
}
