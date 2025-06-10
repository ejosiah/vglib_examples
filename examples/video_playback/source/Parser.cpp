#define MINIMP4_IMPLEMENTATION
#include "Parser.hpp"
#ifdef _WIN32
#include <sys/types.h>
#include <cstddef>
typedef size_t ssize_t;
#endif
#include "Video.hpp"
#include "video/minimp4.hpp"
#include "video/h264.hpp"
#include <spdlog/spdlog.h>

struct INPUT_BUFFER {
    const uint8_t * buffer;
    size_t size;
};

int read_callback(int64_t offset, void* buffer, size_t size, void* token) {
    auto buf = reinterpret_cast<INPUT_BUFFER*>(token);
    auto cpySize = MINIMP4_MIN(size, buf->size - offset - size);
    std::memcpy(buffer, buf->buffer + offset, cpySize);

    return cpySize != size;
}

void calculatePictureOrder(std::shared_ptr<Video> video) {
    if (video->profile == VideoProfile::H264){
        const h264::SliceHeader* slice_header_array = (const h264::SliceHeader*)video->slice_header_datas.data();
        const h264::PPS* pps_array = (const h264::PPS*)video->pps_datas.data();
        const h264::SPS* sps_array = (const h264::SPS*)video->sps_datas.data();
        int prev_pic_order_cnt_lsb = 0;
        int prev_pic_order_cnt_msb = 0;
        int poc_cycle = 0;
        for (size_t i = 0; i < video->frame_infos.size(); ++i){
            const h264::SliceHeader& slice_header = slice_header_array[i];
            const h264::PPS& pps = pps_array[slice_header.pic_parameter_set_id];
            const h264::SPS& sps = sps_array[pps.seq_parameter_set_id];

            // Rec. ITU-T H.264 (08/2021) page 77
            int max_pic_order_cnt_lsb = 1 << (sps.log2_max_pic_order_cnt_lsb_minus4 + 4);
            int pic_order_cnt_lsb = slice_header.pic_order_cnt_lsb;

            if (pic_order_cnt_lsb == 0)
            {
                poc_cycle++;
            }

            // Rec. ITU-T H.264 (08/2021) page 115
            // Also: https://www.ramugedia.com/negative-pocs
            int pic_order_cnt_msb = 0;
            if (pic_order_cnt_lsb < prev_pic_order_cnt_lsb && (prev_pic_order_cnt_lsb - pic_order_cnt_lsb) >= max_pic_order_cnt_lsb / 2)
            {
                pic_order_cnt_msb = prev_pic_order_cnt_msb + max_pic_order_cnt_lsb; // pic_order_cnt_lsb wrapped around
            }
            else if (pic_order_cnt_lsb > prev_pic_order_cnt_lsb && (pic_order_cnt_lsb - prev_pic_order_cnt_lsb) > max_pic_order_cnt_lsb / 2)
            {
                pic_order_cnt_msb = prev_pic_order_cnt_msb - max_pic_order_cnt_lsb; // here negative POC might occur
            }
            else
            {
                pic_order_cnt_msb = prev_pic_order_cnt_msb;
            }
            //pic_order_cnt_msb = pic_order_cnt_msb % 256;
            prev_pic_order_cnt_lsb = pic_order_cnt_lsb;
            prev_pic_order_cnt_msb = pic_order_cnt_msb;

            // https://www.vcodex.com/h264avc-picture-management/
            video->frame_infos[i].poc = pic_order_cnt_msb + pic_order_cnt_lsb; // poc = TopFieldOrderCount
            video->frame_infos[i].gop = poc_cycle - 1;
        }
    }
    if (video->profile == VideoProfile::H265){
        assert(0); // TODO
    }

    std::vector<size_t> frame_display_order(video->frame_infos.size());
    for (size_t i = 0; i < video->frame_infos.size(); ++i){
        frame_display_order[i] = i;
    }
    std::sort(frame_display_order.begin(), frame_display_order.end(), [&](size_t a, size_t b) {
        const Video::FrameInfo& frameA = video->frame_infos[a];
        const Video::FrameInfo& frameB = video->frame_infos[b];
        int64_t prioA = (int64_t(frameA.gop) << 32ll) | int64_t(frameA.poc);
        int64_t prioB = (int64_t(frameB.gop) << 32ll) | int64_t(frameB.poc);
        return prioA < prioB;
    });
    float timestamp = 0;
    for (size_t i = 0; i < frame_display_order.size(); ++i){
        Video::FrameInfo& frame_info = video->frame_infos[frame_display_order[i]];
        frame_info.display_order = (int)i;
        frame_info.timestamp_seconds = timestamp;
        timestamp += frame_info.duration_seconds;
    }
}


video::Parser::Parser(VulkanDevice& device) : m_device(&device){}

std::shared_ptr<Video> video::Parser::parse(const std::filesystem::path& path) {

    auto video = std::make_shared<Video>();

    auto video_data = loadFile(path.string());
    auto input_buffer = INPUT_BUFFER{ as<uint8_t>(video_data.data()), video_data.size() };

    MP4D_demux_t mp4{};

    auto result = MP4D_open(&mp4, read_callback, &input_buffer, to<uint64_t>(video_data.size()));
    if(result == 1) {
        if (mp4.tag.title){
            video->title = as<char>(mp4.tag.title);
        }
        if (mp4.tag.album){
            video->album = as<char>(mp4.tag.album);
        }
        if (mp4.tag.artist){
            video->artist = as<char>(mp4.tag.artist);
        }
        if (mp4.tag.year){
            video->year = as<char>(mp4.tag.year);
        }
        if (mp4.tag.comment){
            video->comment = as<char>(mp4.tag.comment);
        }
        if (mp4.tag.genre){
            video->genre = as<char>(mp4.tag.genre);
        }

        VideoCapabilities capabilities{};
        Video::getCapabilities(*m_device, capabilities);
        const auto alignment = capabilities.capabilities.minBitstreamBufferSizeAlignment;

        for(auto ntrack = 0u; ntrack < mp4.track_count; ++ntrack) {
            const auto& track = mp4.track[ntrack];
            if (track.handler_type == MP4D_HANDLER_TYPE_VIDE){
                switch (track.object_type_indication){
                    case MP4_OBJECT_TYPE_AVC:
                        video->profile = VideoProfile::H264;
                        break;
                    case MP4_OBJECT_TYPE_HEVC:
                        video->profile = VideoProfile::H265;
                        spdlog::warn("H265 (HEVC) video format is not supported yet!");
                        return {};
                    default:
                        spdlog::warn("Unknown video format! track.object_type_indication = {}", to<int>(track.object_type_indication));
                        return {};
                }

                // SPS:
                {
                    int size = 0;
                    int index = 0;
                    const void* data = nullptr;
                    while ((data = MP4D_read_sps(&mp4, ntrack, index, &size))) {
                        auto sps_data = as<const uint8_t>(data);


                        h264::Bitstream bs = {};
                        bs.init(sps_data, size);
                        h264::NALHeader nal = {};
                        h264::read_nal_header(&nal, &bs);
                        assert(nal.type == h264::NAL_UNIT_TYPE_SPS);

                        h264::SPS sps = {};
                        h264::read_sps(&sps, &bs);

                        // Some validation checks that data parsing returned expected values:
                        //	https://stackoverflow.com/questions/6394874/fetching-the-dimensions-of-a-h264video-stream
                        uint32_t width = ((sps.pic_width_in_mbs_minus1 + 1) * 16) - sps.frame_crop_left_offset * 2 - sps.frame_crop_right_offset * 2;
                        uint32_t height = ((2 - sps.frame_mbs_only_flag) * (sps.pic_height_in_map_units_minus1 + 1) * 16) - (sps.frame_crop_top_offset * 2) - (sps.frame_crop_bottom_offset * 2);
                        assert(track.SampleDescription.video.width == width);
                        assert(track.SampleDescription.video.height == height);
                        video->padded_width = (sps.pic_width_in_mbs_minus1 + 1) * 16;
                        video->padded_height = (sps.pic_height_in_map_units_minus1 + 1) * 16;
                        video->num_dpb_slots = std::max(video->num_dpb_slots, uint32_t(sps.num_ref_frames + 1));

                        video->sps_datas.push_back(sps);
                        video->sps_count++;
                        index++;
                    }
                }

                // PPS:
                {
                    int size = 0;
                    int index = 0;
                    while (auto data = MP4D_read_pps(&mp4, ntrack, index, &size)){
                        auto pps_data = as<const uint8_t>(data);

                        h264::Bitstream bs = {};
                        bs.init(pps_data, size);
                        h264::NALHeader nal = {};
                        h264::read_nal_header(&nal, &bs);
                        assert(nal.type == h264::NAL_UNIT_TYPE_PPS);

                        h264::PPS pps = {};
                        h264::read_pps(&pps, &bs);
                        video->pps_datas.push_back(pps);
                        video->pps_count++;
                        index++;
                    }
                }

                video->width = track.SampleDescription.video.width;
                video->height = track.SampleDescription.video.height;
                video->bit_rate = track.avg_bitrate_bps;

                double timescale_rcp = 1.0 / double(track.timescale);

                video->frame_infos.reserve(track.sample_count);
                video->slice_header_datas.resize(track.sample_count);
                video->slice_header_count = track.sample_count;
                uint32_t track_duration = 0;
                uint64_t aligned_size = 0;
                for (uint32_t i = 0; i < track.sample_count; i++)
                {
                    unsigned frame_bytes, timestamp, duration;
                    MP4D_file_offset_t ofs = MP4D_frame_offset(&mp4, ntrack, i, &frame_bytes, &timestamp, &duration);
                    track_duration += duration;

                    Video::FrameInfo& info = video->frame_infos.emplace_back();
                    info.offset = aligned_size;

                    const uint8_t* src_buffer = input_buffer.buffer + ofs;
                    while (frame_bytes > 0)
                    {
                        uint32_t size = ((uint32_t)src_buffer[0] << 24) | ((uint32_t)src_buffer[1] << 16) | ((uint32_t)src_buffer[2] << 8) | src_buffer[3];
                        size += 4;
                        assert(frame_bytes >= size);

                        h264::Bitstream bs = {};
                        bs.init(&src_buffer[4], frame_bytes);
                        h264::NALHeader nal = {};
                        h264::read_nal_header(&nal, &bs);

                        if (nal.type == h264::NAL_UNIT_TYPE_CODED_SLICE_IDR)
                        {
                            info.type = VideoFrameType::Intra;
                        }
                        else if (nal.type == h264::NAL_UNIT_TYPE_CODED_SLICE_NON_IDR)
                        {
                            info.type = VideoFrameType::Predictive;
                        }
                        else
                        {
                            // Continue search for frame beginning NAL unit:
                            frame_bytes -= size;
                            src_buffer += size;
                            continue;
                        }

                        h264::SliceHeader& slice_header = video->slice_header_datas[i];
                        h264::read_slice_header(&slice_header, &nal, (const h264::PPS*)video->pps_datas.data(), (const h264::SPS*)video->sps_datas.data(), &bs);

                        // Accept frame beginning NAL unit:
                        info.reference_priority = nal.idc;
                        info.size = sizeof(h264::nal_start_code) + size - 4;
                        break;
                    }

                    aligned_size += alignedSize(info.size, alignment);
                    info.duration_seconds = float(double(duration) * timescale_rcp);
                }


                calculatePictureOrder(video);

                video->average_frames_per_second = float(double(track.timescale) / double(track_duration) * track.sample_count);
                video->duration_seconds = float(double(track_duration) * timescale_rcp);

                auto copy_video_track = [&](void* dest) {
                    for (uint32_t i = 0; i < track.sample_count; i++)
                    {
                        unsigned frame_bytes, timestamp, duration;
                        MP4D_file_offset_t ofs = MP4D_frame_offset(&mp4, ntrack, i, &frame_bytes, &timestamp, &duration);
                        uint8_t* dst_buffer = (uint8_t*)dest + video->frame_infos[i].offset;
                        const uint8_t* src_buffer = input_buffer.buffer + ofs;
                        while (frame_bytes > 0)
                        {
                            uint32_t size = ((uint32_t)src_buffer[0] << 24) | ((uint32_t)src_buffer[1] << 16) | ((uint32_t)src_buffer[2] << 8) | src_buffer[3];
                            size += 4;
                            assert(frame_bytes >= size);

                            h264::Bitstream bs = {};
                            bs.init(&src_buffer[4], sizeof(uint8_t));
                            h264::NALHeader nal = {};
                            h264::read_nal_header(&nal, &bs);

                            if (
                                    nal.type != h264::NAL_UNIT_TYPE_CODED_SLICE_IDR &&
                                    nal.type != h264::NAL_UNIT_TYPE_CODED_SLICE_NON_IDR
                                    )
                            {
                                frame_bytes -= size;
                                src_buffer += size;
                                continue;
                            }

                            std::memcpy(dst_buffer, h264::nal_start_code, sizeof(h264::nal_start_code));
                            std::memcpy(dst_buffer + sizeof(h264::nal_start_code), src_buffer + 4, size - 4);
                            break;
                        }
                    }
                };

                VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
                bufferInfo.pNext = &capabilities.profiles;
                bufferInfo.size = aligned_size;
                bufferInfo.usage = VK_BUFFER_USAGE_VIDEO_DECODE_SRC_BIT_KHR;
                bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                video->data_stream = m_device->createBuffer(bufferInfo, VMA_MEMORY_USAGE_CPU_TO_GPU);
                m_device->setName<VK_OBJECT_TYPE_BUFFER>(fmt::format("{}_video_stream", path.filename().string()), video->data_stream.buffer);

                byte_string video_stream(aligned_size);
                auto data = video->data_stream.map();
                copy_video_track(data);
                video->data_stream.unmap();
            }
            else if (track.handler_type == MP4D_HANDLER_TYPE_SOUN)
            {
                spdlog::warn("Audio from video file is not implemented yet!");
            }
        }
    }

    return video;
}

