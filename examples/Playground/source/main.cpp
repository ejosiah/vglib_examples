#include "common.h"
#include "primitives.h"
#include "SDKmesh.h"
#include "Mesh.h"

#include <span>

#include <glm/glm.hpp>
#include <fmt/core.h>
#include <numeric>
#include <stb_image_write.h>
#include "vulkan_context.hpp"
#include "random.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#ifndef STBI_MSC_SECURE_CRT
#define STBI_MSC_SECURE_CRT
#ifndef STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#endif // STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#endif // STBI_MSC_SECURE_CRT

auto GSeris(auto a, auto r, auto n) {
    return a * std::pow(r, n - 1);
}

struct char4 {
    int8_t a, b, c, d;
};

int main() {
    auto rngX = rng(0.f, 1.f);
    auto rngY = rng(0.f, 1.f);
    auto rngZ = rng(0.f, 1.f);
    auto rngW = rng(0.f, 1.f);
    std::vector<char4> randoms(1024 * 1024);
    std::generate(randoms.begin(), randoms.end(), [&]{
        return  char4{
                    to<int8_t>(rngX() * 255),
                    to<int8_t>(rngY() * 255),
                    to<int8_t>(rngZ() * 255),
                    to<int8_t>(rngW() * 255)}; } );


    auto w = 1024;
    auto c = 4;
    stbi_write_png("random4.png", w, w, c, randoms.data(), w * c);
}
