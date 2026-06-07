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

#include "mnist/mnist_loader.hpp"
#include <fmt/format.h>
#include <cpr/cpr.h>
#include "sun_calc.hpp"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <linear_system.hpp>
#include <linalg/linalg.hpp>
#include "linear_solver_eval.hpp"

auto GSeris(auto a, auto r, auto n) {
    return a * std::pow(r, n - 1);
}

struct char4 {
    int8_t a, b, c, d;
};

void random_image() {
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

int main(int argc, char** argv) {
    fs::current_path("../../../../examples/");
    FileManager::instance().addSearchPathFront("../data");

    constexpr size_t system_size = 16 * 16;
    auto system = linear_system::load(FileManager::resource("linear_system/euler_spd_16.txt"));
    auto dm = linear_system::create_matrix<linalg::dense_row_matrix<float, system_size, system_size>>(system);
    auto sm = linear_system::create_matrix<linalg::sparse_csr_matrix<float, system_size, system_size>>(system);
    auto b = linear_system::create_vector<linalg::vector<float, system_size>>(system);
    auto source = linear_solver_eval::make_dataset_info(system, "euler_spd_16");

    linear_solver_eval::run(source, dm, sm, b, argc, argv);

}
