#include "cbt/large/cbt.h"

namespace cbt_large {

    void initialize_gpu_cbt(const CBT& cbt, const VulkanDevice& device, GPU_CBT& gpuCBT)
    {

        // Create the graphics buffer to upload, process and readback the bitfield buffer
        gpuCBT.bufferCount = cbt.num_internal_buffers();
        gpuCBT.lastLevelSize = cbt.last_level_size();
        for (uint32_t bufferIdx = 0; bufferIdx < gpuCBT.bufferCount; ++bufferIdx)
        {
            uint32_t bufferSize = cbt.buffer_size(bufferIdx);
            gpuCBT.bufferArray[bufferIdx] = device.createDeviceLocalBuffer(cbt.raw_buffer(bufferIdx), bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

        }
    }

    void release_gpu_cbt(GPU_CBT& gpuCBT) {

        // Reset the values
        gpuCBT.numElements = 0;
        gpuCBT.bufferCount = 0;
        gpuCBT.lastLevelSize = 0;
         auto temp = std::move(gpuCBT);
    }

}
