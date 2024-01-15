#include <fmt/format.h>
#include <fmt/ranges.h>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <functional>
#include <cmath>
#include <span>
#include <numeric>
#include <algorithm>
#include <array>
#include <random>
#include <vector>
#include <optional>
#include <fstream>
#include "primitives.h"
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include "add_ts_vectors.h"
#include "shader_reflect.hpp"
#include "xforms.h"

glm::vec2 randomPointOnSphericalAnnulus(glm::vec2 x, float r, auto rng) {
    float angle = rng() * glm::two_pi<float>();
    glm::vec2 d{glm::cos(angle), glm::sin(angle)};
    float t = rng() * r + r;
    return x + d * t;
}

glm::vec2 randomPoint(glm::vec2 domain, auto rng) {
    return domain * glm::vec2(rng(), rng());    // FIXME domain does not always start at [0, 0]
}

int gridIndex(glm::vec2 point, glm::ivec2 gridSize, float w) {
    auto pid = glm::ivec2(glm::floor(point/w));
    return glm::clamp(pid.y * gridSize.x + pid.x, 0, gridSize.x * gridSize.y - 1);
}

std::optional<glm::vec2> sample(std::span<glm::vec2> grid, glm::vec2 domain, glm::vec2 x, float r, int k, auto rng) {
    float w = r/glm::sqrt(2.f);
    glm::ivec2 gridSize = glm::ivec2(floor(domain/w));
    for(auto i = 0; i < k; i++){
        auto x1 = randomPointOnSphericalAnnulus(x, r, rng);
        auto coord = glm::ivec2(floor(x1/w));
        if(coord.x < 0 || coord.x >= gridSize.x || coord.y < 0 || coord.y >= gridSize.y){
            continue;
        }
        bool farEnough = true;
        for(int rr = -1; rr <= 1 && farEnough; rr++){
            for(int c = -1; c <= 1; c++){
                auto pid = coord + glm::ivec2(c, rr);

                if(pid.x >= 0 && pid.x < gridSize.x && pid.y >= 0 && pid.y < gridSize.y){
                    auto index = pid.y * gridSize.x + pid.x;
                    if(index >= grid.size()){
                        index  = pid.y * gridSize.x + pid.x;
                    }
                    auto neighbour = grid[index];
                    if(glm::any(glm::isnan(neighbour))) continue;
                    if(glm::distance(x1, neighbour) < r){
                        farEnough = false;
                        break;
                    }
                }
            }
        }
        if(farEnough){
            return x1;
        }
    }
    return {};
}

std::vector<glm::vec2> poissonDiskSampling(glm::vec2 domain, float r, int k = 30) {
    float w = r/glm::sqrt(2.f);
    glm::ivec2 gridSize = glm::ivec2(floor(domain/w));
    const auto N = gridSize.x * gridSize.y;
    std::vector<glm::vec2> grid(N, glm::vec2(NAN));
    std::vector<glm::vec2> activeList{};

    std::uniform_real_distribution<float> dist{0, 0.99999999999};
    std::default_random_engine engine{1 << 20};
    auto rng = [&]{ return dist(engine); };

    auto x = randomPoint(domain, rng);
    auto index = gridIndex(x, gridSize, w);
    grid[index] = x;
    activeList.push_back(x);

    int count = 0;
    while(!activeList.empty()){
//        fmt::print("{}: activeList: {}\n",count, activeList.size());
        auto ai = int(glm::floor(rng() * activeList.size()));
        x = activeList[ai];

        auto x1 = sample(grid, domain, x, r, k, rng);
        if(x1.has_value()){
            index = gridIndex(*x1, gridSize, w);
            grid[index] = *x1;
            activeList.push_back(*x1);
        }else {
            activeList.erase(std::find(activeList.begin(), activeList.end(), x));
        }
        count++;
    }

    std::vector<glm::vec2> points{};
    for(const auto& point : grid){
        if(!glm::any(glm::isnan(point))){
            points.push_back(point);
        }
    }

    return grid;
}

std::istream& operator>>(std::istream& in, glm::vec3& v) {
    return in >> v.x >> v.y >> v.z;
}

int main(int argc, char** argv){
//    auto samples = poissonDiskSampling({8, 8}, glm::sqrt(2.f) * .1f);
//    fmt::print("generated {} points\n", samples.size());
//    std::ofstream fout{R"(D:\Program Files\SHADERed\poisson.dat)", std::ios::binary};
//    if(!fout.good()){
//        fmt::print("unable to open file for writing\n");
//    }
//    long long size = samples.size() * sizeof(glm::vec2);
//    auto data = reinterpret_cast<char*>(samples.data());
//    fout.write(data, size);
//    fmt::print("samples written to disk");
////    for(const auto& sample : samples){
////        fmt::print("[{}, {}]\n", sample.x, sample.y);
////    }
//    auto cone = primitives::cone(50, 50, 1, 1, glm::vec4(1, 0, 0, 1), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
//    fmt::print("num triangles {}\n", cone.indices.size()/3);
//
//    std::vector<glm::vec3> positions{};
//    std::vector<glm::vec4> normals{};
//    std::vector<glm::vec3> combined{};
//
//    for(int i = 0; i < cone.indices.size(); i+= 3){
//        auto v0 = cone.indices[i];
//        auto v1 = cone.indices[i+1];
//        auto v2 = cone.indices[i+2];
//
//        positions.push_back(cone.vertices[v0].position.xyz());
//        positions.push_back(cone.vertices[v1].position.xyz());
//        positions.push_back(cone.vertices[v2].position.xyz());
//
//
//
//        normals.push_back(glm::vec4(cone.vertices[v0].normal, 0));
//        normals.push_back(glm::vec4(cone.vertices[v1].normal, 0));
//        normals.push_back(glm::vec4(cone.vertices[v2].normal, 0));
//
//        combined.push_back(cone.vertices[v0].position.xyz());
//        combined.push_back(cone.vertices[v0].normal);
//
//        combined.push_back(cone.vertices[v1].position.xyz());
//        combined.push_back(cone.vertices[v1].normal);
//
//        combined.push_back(cone.vertices[v2].position.xyz());
//        combined.push_back(cone.vertices[v2].normal);
//    }
//
//    fmt::print("num vertices {}\n", positions.size());
//
//    std::ofstream fout{R"(D:\Program Files\SHADERed\cone_position.dat)", std::ios::binary};
//    if(!fout.good()){
//        fmt::print("unable to open file for writing\n");
//    }
//    long long size = positions.size() * sizeof(glm::vec3);
//    auto data = reinterpret_cast<char*>(positions.data());
//    fout.write(data, size);
//    fout.close();
//    fmt::print("positions written to disk\n");
//
//    fout = std::ofstream{R"(D:\Program Files\SHADERed\cone_normal.dat)", std::ios::binary};
//    if(!fout.good()){
//        fmt::print("unable to open file for writing\n");
//    }
//     size = normals.size() * sizeof(glm::vec4);
//    data = reinterpret_cast<char*>(normals.data());
//    fout.write(data, size);
//    fout.close();
//    fmt::print("normals written to disk\n");
//
//    fout = std::ofstream{R"(D:\Program Files\SHADERed\cone_interlaced.dat)", std::ios::binary};
//    if(!fout.good()){
//        fmt::print("unable to open file for writing\n");
//    }
//     size = combined.size() * sizeof(glm::vec3);
//    data = reinterpret_cast<char*>(combined.data());
//    fout.write(data, size);
//    fout.close();
//    fmt::print("cone data written to disk\n");

  //  add_tangent_space_vectors(R"(C:\Users\Josiah Ebhomenye\OneDrive\media\models\bs_ears.obj)", R"(D:\Program Files\SHADERed\olga.dat)");


//    Assimp::Exporter exporter{};
//    auto count = exporter.GetExportFormatCount();
//    fmt::print("3d export formats supported: {}\n", count);
//
//    for(auto i = 0; i < count; i++){
//        auto desc = exporter.GetExportFormatDescription(i);
//        fmt::print("\t[id: {}, description: {}, extension: {}]\n", desc->id, desc->description, desc->fileExtension);
//    }
//    shader_reflect(argc, argv);

    auto projection = vkn::perspectiveVFov(glm::half_pi<float>(), 1, 1, 100);
//    auto projection = glm::perspective(glm::half_pi<float>(), 1.f, 1.f, 100.f);

    auto linearToRawDepth = [](float depth, float near, float far) {
        return ( near * far ) / ( depth * ( near - far ) ) - far / ( near - far );
    };

    glm::vec4 x{0, 0, -1, 1};

    auto xp = projection * x;
    fmt::print("xp: {}\n", xp);
    xp /= xp.w;
    fmt::print("xp/w: {}\n", xp.z);

    x.z = -100;
    xp = projection * x;
    fmt::print("xp: {}\n", xp);
    xp /= xp.w;
    fmt::print("xp/x: {}\n", xp.z);

    auto wz = linearToRawDepth(xp.z, 1, 100);
    fmt::print("world z: {}\n", wz);

    fmt::print("vertex: {}", sizeof(Vertex));
}