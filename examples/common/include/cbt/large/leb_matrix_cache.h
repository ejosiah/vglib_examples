#pragma once

// Project includes
#include "VulkanDevice.h"

namespace cbt_large {

    class LebMatrixCache
    {
    public:
        // Cst & Dst
        LebMatrixCache();
        ~LebMatrixCache();

        // Init & Release
        void intialize(const VulkanDevice& device, uint32_t cacheDepth);

        void release();

        // Access the buffer
        VulkanBuffer get_leb_matrix_buffer() const {return m_LebMatrixBuffer;}

    private:
        VulkanBuffer m_LebMatrixBuffer;
        uint32_t m_CacheDepth ;
    };

}

using LebMatrixCache = cbt_large::LebMatrixCache;
