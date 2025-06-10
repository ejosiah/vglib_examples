#pragma once

#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "Texture.h"

#include "video/h264.hpp"
#include "EnumBitFlags.hpp"

#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <optional>

struct VideoCapabilities {
    std::vector<VkVideoFormatPropertiesKHR> formats;
    VkVideoDecodeH264CapabilitiesKHR h264DecodeCapabilities;
    VkVideoDecodeCapabilitiesKHR decodeCapabilities;
    VkVideoDecodeH264ProfileInfoKHR h264ProfileInfo;
    VkVideoCapabilitiesKHR capabilities;
    VkVideoProfileListInfoKHR profiles;
    VkVideoProfileInfoKHR profile;
};

struct Video {
    enum class Profile { H264, H265 };

    struct FrameInfo{
        enum class Type { Intra, Predictive };
        uint64_t offset = 0;
        uint64_t size = 0;
        float timestamp_seconds = 0;
        float duration_seconds = 0;
        Type type = Type::Intra;
        uint32_t reference_priority = 0;
        int poc = 0;
        int gop = 0;
        int display_order = 0;
    };

    std::string title;
    std::string album;
    std::string artist;
    std::string year;
    std::string comment;
    std::string genre;
    uint32_t padded_width = 0;
    uint32_t padded_height = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bit_rate = 0;
    Profile profile = Profile::H264;

    std::vector<h264::SPS> sps_datas;
    std::vector<h264::PPS> pps_datas;
    std::vector<h264::SliceHeader> slice_header_datas;
    uint32_t sps_count = 0;
    uint32_t pps_count = 0;
    uint32_t slice_header_count = 0;
    VulkanBuffer data_stream;
    float average_frames_per_second = 0;
    float duration_seconds = 0;
    std::vector<FrameInfo> frame_infos;
    uint32_t num_dpb_slots = 0;

    static void getCapabilities(VulkanDevice& device, VideoCapabilities& capabilities);

    static uint64_t getBitStringAlignment(VulkanDevice& device);

};

using VideoProfile = Video::Profile;
using VideoFrameType = Video::FrameInfo::Type;

struct VideoSession {
    VkVideoSessionKHR handle{};
    VkVideoSessionParametersKHR parameters{};
    std::vector<VkDeviceMemory> allocations;
};

struct ResourceState {
    VkImageLayout layout{};
    VkPipelineStageFlagBits2 stage{};
    VkAccessFlags2 access{};
};

struct OutputTexture{
    struct {
        Texture texture; // resolved RGB image
        VkImageSubresourceRange subresource_luminance{};
        VkImageSubresourceRange subresource_chrominance{};
        ResourceState state;
    } display{};
    // Below can be either point to DPB in coincide mode, or separate decoder output in non-coincide mode:
    struct {
        Texture* texture;
        VkImageSubresourceRange subresource_luminance{};
        VkImageSubresourceRange subresource_chrominance{};
        ResourceState state;
    } src;
    std::string name;
    int display_order{-1};
};

struct DPB{
    Texture texture; // raw decoder image array (only can be sampled when device supports coincide mode decoder)
    std::array<VkImageSubresourceRange, 17> subresources_luminance;
    std::array<VkImageSubresourceRange, 17> subresources_chrominance;
    std::array<int, 17> poc_status;
    std::array<int, 17> framenum_status;
    std::array<ResourceState, 17> resource_states;
    std::vector<uint8_t> reference_usage;
    uint8_t next_ref;
    uint8_t next_slot;
    uint8_t current_slot;
};

struct VideoInstance{

    enum class Flags
    {
        Empty = 0,
        Playing = 1 << 0,
        Looped = 1 << 1,
        Mipmapped = 1 << 2,
        NeedsResolve = 1 << 3,
        FirstFrameDecoded = 1 << 4,
        DecoderReset = 1 << 5,
    };
    const std::shared_ptr<Video> video;
    DPB dpb;
    std::vector<OutputTexture> output_textures_free; // free images that can be reused for display order buffering
    std::vector<uint32_t> output_textures_resolve_request; // request image to be resolved
    std::vector<OutputTexture> output_textures_used; // resolved image for future display ordering use
    OutputTexture output; // Currently displayed RGB image with the latest display order
    int target_display_order = 0; // the current display order that should be visible
    int current_decode_frame = 0; // the latest decoded frame index
    size_t maxFrames = std::numeric_limits<size_t>::max();
    float current_time = 0; // tracking the absolute time of the playback in seconds
    Flags flags = Flags::Empty;
    VideoSession session;

    void seek(float timeInSeconds);

    void update(float timeInSeconds);

    void updateDisplayOrderOutput();

    void resolveToDisplay(VkCommandBuffer commandBuffer,VulkanDevice& device);

    bool isDecodingRequired(VulkanDevice& device);

    OutputTexture* findNextDisplayableOutput();

    uint32_t width() const;

    uint32_t height() const;
};

struct VideoDecodeOperation {
    enum  Flag : int {
        EMPTY = 0,
        SESSION_RESET = 1 << 0, // first usage of decoder needs reset
    };
    int flags = Flag::EMPTY;
    VulkanBuffer stream;
    uint64_t stream_offset{}; // must be aligned with GraphicsDevice::GetVideoDecodeBitstreamAlignment()
    uint64_t stream_size{};
    VideoFrameType frame_type = VideoFrameType::Intra;
    uint32_t reference_priority{}; // nal_ref_idc from nal unit header
    int decoded_frame_index{}; // frame index in order of decoding
    const h264::SliceHeader* slice_header{}; // slice header for current frame
    const h264::PPS* pps{}; // picture parameter set for current slice header
    const h264::SPS* sps{}; // sequence parameter set for current picture parameter set
    std::array<int, 2> poc; // PictureOrderCount Top and Bottom fields
    uint32_t current_dpb{}; // DPB slot for current output picture
    uint8_t dpb_reference_count{}; // number of references in dpb_reference_slots array
    std::span<uint8_t> dpb_reference_slots{}; // dpb slot indices that are used as reference pictures
    std::span<int> dpb_poc{}; // for each DPB reference slot, indicate the PictureOrderCount
    std::span<int> dpb_framenum{}; // for each DPB reference slot, indicate the framenum value
    const Texture* DPB{}; // DPB texture with arraysize = num_references + 1
    const Texture* output{}; // output of the operation, it should be nullptr if DPB_AND_OUTPUT_COINCIDE is used (because in that case the DPB will be used as output instead of a separate output)
};

template<>
struct enable_bitmask_operators<VideoInstance::Flags> {
    static const bool enable = true;
};