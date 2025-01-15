#include "common.h"
#include "GroundPatch.hpp"
#include <glm/glm.hpp>
#include <cmath>
#include <functional>

GroundPatch::GroundPatch(VulkanDevice &device,  int levels, float scale)
: device_{&device}
, levels_{levels}
, scale_{scale}
, base2Scale_{1 << levels}
{
    auto index = 0ul;
    auto mask = static_cast<unsigned long>(base2Scale_);
    _BitScanReverse(&index, mask);
    highest_bit_ = static_cast<int>(index);
}

void GroundPatch::init() {
    const auto N = (16 * (levels_ - 1)) - (4 * (levels_ - 2));
    auto size = VkDeviceSize{sizeof(glm::vec3) * N};
    quad_tree_.resize(size);
    patch_buffer_ = device_->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, size, "ground_patches");
    patches_ = patch_buffer_.span<glm::vec3>();
}

void GroundPatch::update(const BaseCameraController &camera) {
    auto bMin = glm::vec3();
    auto bMax = glm::vec3();
    camera.extractAABB(bMin, bMax);
    bMin.y = 0;
    bMax.y = 0;

    auto in_quadrant = [](auto pMin, auto pMax, auto cMin, auto cMax) {
       return glm::any(glm::lessThan(cMin, pMin)) || glm::any(glm::greaterThan(cMax, pMax));
    };

    std::vector<glm::vec3> patches;

    std::function<void(std::vector<glm::vec3>, float, int)> loop = [&](auto current_patch, auto scale, auto current_level) {
        if(current_level == levels_) {
            return true;
        }

        const auto half_scale = scale * 0.5f;

        std::array<bool, 4> inChildren{};
        decltype(quadrants_) child_patches;

        for(auto i = 0; i < 4; ++i) {
            auto child_patch = decltype(quad_){};
            auto pMin = glm::vec3(MAX_FLOAT, 0, MAX_FLOAT);
            auto pMax = glm::vec3(MIN_FLOAT, 0, MIN_FLOAT);
            for(auto j = 0; j < 4; ++j) {
                auto p = quad_[j] * quadrants_[i][j];
                pMin = glm::min(pMin, p);
                pMax = glm::max(pMax, p);
                inChildren[i] = in_quadrant(pMin, pMax, bMin, bMax);
                child_patches[i][j] = p;
            }
        }

        auto itr0 = std::find_if(inChildren.begin(), inChildren.end(), [](const auto& b){ return b; });
        auto itr1 = std::find_if(itr0, inChildren.end(), [](const auto& b){ return b; });
        auto crossesQuadrants = itr0 != inChildren.end() && itr1 != inChildren.end();
        if(crossesQuadrants) {
            patches.push_back(current_patch[0]);
            patches.push_back(current_patch[1]);
            patches.push_back(current_patch[2]);
            patches.push_back(current_patch[3]);
        } else {
            auto quadrant = itr0 - inChildren.begin();
            loop(child_patches[quadrant], half_scale, current_level+1);
        }
    };

    decltype(quad_) patch{};
    std::ranges::transform(quad_, patch.begin(), [&](auto v){ return v * scale_; });
    loop(patch, scale_, 0);

}

const VulkanBuffer &GroundPatch::patch() const {
    return patch_buffer_;
}
