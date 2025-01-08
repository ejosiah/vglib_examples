#include <glm/gtc/matrix_transform.hpp>
#include <fmt/format.h>
#include <glm/gtc/matrix_access.hpp>

#include <algorithm>
#include <vector>
#include <glm/glm.hpp>
#include "vol_loader.hpp"

using namespace glm;

float densityFunction(vec3 x) {
    return -x.y;
}

int main() {
    auto path = R"(C:\Users\joebh\Downloads\GPU Gems 3 code\content\01\demo\textures\packednoise_half_16cubed_mips_01.vol)";

    auto format = vol_load(path);

    fmt::print("{}\n", format.header.title);
}