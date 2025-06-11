#include <filesystem>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <fmt/format.h>
#include <glm_format.h>
#include "nishita.hpp"
#include "ltc.hpp"
#include <fstream>
constexpr auto maxDepth = 20UL;
constexpr auto iniMaxDepth = maxDepth;

const char* gProgName = "";
const float earthRadius = 6360 * km;
namespace fs = std::filesystem;

int main() {

    fmt::print("g_ltc_mat {}, {}\n", ltc::g_ltc_mat.size(), std::sqrt(ltc::g_ltc_mat.size()/4));
    fmt::print("g_ltc_mag {}, {}\n", ltc::g_ltc_mag.size(), std::sqrt(ltc::g_ltc_mag.size()/4));
//    std::ofstream fout{"../../../../data/ltc/g_ltc_mat.dat", std::ios::binary};
//    if(!fout.good()) {
//        fmt::print("unable to up g_ltc_mat.dat for writing");
//        std::exit(120);
//    }
//
//    auto size = static_cast<std::streamsize>(sizeof(float) * ltc::g_ltc_mat.size());
//
//    fout.write(reinterpret_cast<char*>(ltc::g_ltc_mat.data()), size);
//    fmt::print("g_ltc_mat.dat saved to disc\n");
//
//    fout = std::ofstream{"../../../../data/ltc/g_ltc_mag.dat", std::ios::binary};
//    if(!fout.good()) {
//        fmt::print("unable to up g_ltc_mag.dat for writing");
//        std::exit(120);
//    }
//
//    size = static_cast<std::streamsize>(sizeof(float) * ltc::g_ltc_mag.size());
//    fout.write(reinterpret_cast<char*>(ltc::g_ltc_mag.data()), size);
//    fmt::print("g_ltc_mag.dat saved to disc\n");
}
