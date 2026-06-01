// Project includes
#include "cbt/large/leb_matrix_cache.h"
#include "cbt/large/cbt_utility.h"
#include "cbt/large/operators.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace cbt_large {

    glm::mat3 SplittingMatrix(uint32_t bitValue)
    {
        float b = float(bitValue);
        float c = 1.0f - b;

        return glm::mat3(
             0.0f, 0.5f,    b,
                b, 0.0f,    c,
                c, 0.5f, 0.0f);
    }

    glm::mat3 DecodeSubdivisionMatrix(uint64_t heapID)
    {
        glm::mat3 m{1.0f};
        int32_t depth = find_msb_64(heapID) - 1;
        for (int32_t bitID = depth - 1; bitID >= 0; --bitID)
            m = SplittingMatrix(static_cast<uint32_t>((heapID >> bitID) & 1u)) * m;
        return m;
    }

    LebMatrixCache::LebMatrixCache()
    {
    }

    LebMatrixCache::~LebMatrixCache()
    {
    }

    void LebMatrixCache::intialize(const VulkanDevice& device, uint32_t cacheDepth)
    {
        // Keep the cache depth
        m_CacheDepth = cacheDepth;
        uint32_t matrixCount = 2ULL << m_CacheDepth;

        // Create the runtime buffer
        // Build the CPU table
        std::vector<glm::mat3> table(matrixCount);
        table[0] = glm::mat3{1.0f};
        for (uint64_t heapID = 1ULL; heapID < (2ULL << m_CacheDepth); ++heapID)
            table[heapID] = DecodeSubdivisionMatrix(heapID);

        m_LebMatrixBuffer = device.createDeviceLocalBuffer(table.data(), BYTE_SIZE(table),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    }

    void LebMatrixCache::release()
    {
        auto temp = std::move(m_LebMatrixBuffer);
    }

}
