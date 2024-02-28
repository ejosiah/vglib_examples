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
#include <openvdb/openvdb.h>
#include <openvdb/io/Stream.h>
#include "spectrum/spectrum.hpp"

using namespace glm;

#define QUEUE_CAPACITY 5
#define QUEUE_MODE_NORMAL 0
#define QUEUE_MODE_HEAP 1

struct Queue {
    std::array<int, QUEUE_CAPACITY>  data;
    int length = 0;
    int capacity = QUEUE_CAPACITY;
    int mode = QUEUE_MODE_NORMAL;

    auto begin() { return data.begin(); }

    auto end() { return data.end(); }
};

void push(Queue& queue, int entry){
    if(queue.mode == QUEUE_MODE_NORMAL){
        if(queue.length < queue.capacity){
            queue.data[queue.length] = entry;
            queue.length++;
        }
    }
}

bool isFull(const Queue& queue) {
    return queue.length == queue.capacity;
}

void swap(int& a, int& b){
    int temp = a;
    a = b;
    b = temp;
}

void makeHeap(Queue& queue) {

    auto& data = queue.data;
    auto bottom = queue.length;
    for(int hole = bottom >> 1; hole > 0;) {
        --hole;
        int val = data[hole];
        const int top = hole;
        int index       = hole;

        // Check whether _Idx can have a child before calculating that child's index, since
        // calculating the child's index can trigger integer overflows
        const int Max_sequence_non_leaf = (bottom - 1) >> 1; // shift for codegen
        while (index < Max_sequence_non_leaf) { // move _Hole down to larger child
            index = 2 * index + 2;
            if (data[index] < data[index - 1]) {
                --index;
            }
            data[hole] = data[index];
            hole             = index;
        }

        if (index == Max_sequence_non_leaf && bottom % 2 == 0) { // only child at bottom, move _Hole down to it
            data[hole] = data[bottom];
            hole             = bottom - 1;
        }


        for (index = (hole - 1) >> 1; // shift for codegen
             top < hole && data[index] < val; index = (hole - 1) >> 1) { // shift for codegen
            // move _Hole up to parent
            data[hole] = data[index];
            hole             = index;
        }

        data[hole] = val; // drop _Val into final hole
    }
    

    queue.mode = QUEUE_MODE_HEAP;
}

void popHeap(Queue& queue) {
    auto& data = queue.data;
    auto last = queue.length;
    if (2 <= last) {
        --last;
        int val = data[last];

        //   _STD _Pop_heap_hole_unchecked(_First, _Last, _Last, _STD move(_Val), _Pred);
        data[last] = data[0];

       //  _Pop_heap_hole_by_index(_First, static_cast<_Diff>(0), static_cast<_Diff>(_Last - _First), _STD forward<_Ty>(_Val), _Pred);
        int bottom = last;
        int hole = 0;
        const int top = hole;
        int idx       = hole;

        // Check whether _Idx can have a child before calculating that child's index, since
        // calculating the child's index can trigger integer overflows
        const int maxSequenceNonLeaf = (bottom - 1) >> 1; // shift for codegen
        while (idx < maxSequenceNonLeaf) { // move _Hole down to larger child
            idx = 2 * idx + 2;
            if (data[idx] < data[idx - 1]) {
                --idx;
            }
            data[hole] = data[idx];
            hole             = idx;
        }

        if (idx == maxSequenceNonLeaf && bottom % 2 == 0) { // only child at bottom, move _Hole down to it
            data[hole] = data[bottom - 1];
            hole             = bottom - 1;
        }

      //  _STD _Push_heap_by_index(_First, _Hole, _Top, _STD forward<_Ty>(_Val), _Pred);
        for (idx                                                          = (hole - 1) >> 1; // shift for codegen
             top < hole && data[idx] < val; idx = (hole - 1) >> 1) { // shift for codegen
            // move _Hole up to parent
            data[hole] = data[idx];
            hole             = idx;
        }

        data[hole] = val;
    }
}

void pushHeap(Queue& queue) {
    auto& data = queue.data;
    auto hole        = queue.length - 1;
    if (2 <= queue.length) {
        int val = data[hole];

        for (int idx = (hole - 1) >> 1; hole > 0 && data[idx] < val; idx = (hole - 1) >> 1) {
            // move _Hole up to parent
            data[hole] = data[idx];
            hole = idx;
        }

        data[hole] = val; // drop _Val into final hole
    }
}

int main(int argc, char** argv){
    Queue queue{ .capacity = 5};
    push(queue, 2);
    push(queue, 3);
    push(queue, 4);
    push(queue, 5);
    push(queue, 10);

//    fmt::print("queue: {}", queue.data);

    makeHeap(queue);
//   std::make_heap(queue.begin(), queue.end());
//    fmt::print("\nqueue: {}\n", queue.data);

    for(int i = 0; i < queue.length; i++) {
//        popHeap(queue);
    std::pop_heap(queue.begin(), std::next(queue.end(), -i));
        fmt::print("queue: {}\n", queue.data);
    }

//    queue.data[queue.length - 1] = 8;
////    std::push_heap(queue.begin(), queue.end());
//    pushHeap(queue);
//    fmt::print("\nqueue: {}", queue.data);
    return 0;
}

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

glm::vec3 cosineSampleHemisphere(glm::vec2 xi){
    const auto PI = glm::pi<float>();
    // Uniformly sample disk.
    const float r = sqrt(xi.x);
    const float phi = 2.0f * PI * xi.y;

    glm::vec2 p;
    p.x = r * cos(phi);
    p.y = r * sin(phi);

    return {p, glm::sqrt(glm::max(0.0f, 1.0f - dot(p, p))) };
}

glm::vec3 sampleHemisphere(glm::vec2 u){
    float a = sqrt(u.x);
    float b = glm::two_pi<float>() * u.y;

    glm::vec3 res{a * cos(b), a * sin(b), sqrt(1 - u.x)};

    return res;
}




//int main(int argc, char** argv){
//    auto cornellBox = primitives::cornellBox();
//
//    std::stringstream ss;
//
//    auto light = spectrum::blackbodySpectrum({5000, 1000}).front()/100.f;
//    ss << fmt::format("newmtl Light\n");
//    ss << "\tKa 0 0 0\n";
//    ss << std::format("\tKd {} {} {}\n", light.x, light.y, light.z);
//    ss << "\tKs 0 0 0\n";
//    ss << "\tTf 1 1 1\n";
//    ss << fmt::format("\tillum {}\n", 10);
//    ss << "\tNs 0\n";
//    ss << "\tNi 1\n";
//
//    ss << fmt::format("\nnewmtl White\n");
//    ss << fmt::format("\tKa {} {} {}\n", cornellBox[1].vertices[0].color.r, cornellBox[1].vertices[0].color.g, cornellBox[1].vertices[0].color.b);
//    ss << fmt::format("\tKd {} {} {}\n", cornellBox[1].vertices[0].color.r, cornellBox[1].vertices[0].color.g, cornellBox[1].vertices[0].color.b);
//    ss << "\tKs 1 1 1\n";
//    ss << "\tTf 1 1 1\n";
//    ss << "\tillum 0\n";
//    ss << "\tNs 50\n";
//    ss << "\tNi 1\n";
//
//    ss << fmt::format("\nnewmtl Red\n");
//    ss << fmt::format("\tKa {} {} {}\n", cornellBox[2].vertices[0].color.r, cornellBox[2].vertices[0].color.g, cornellBox[2].vertices[0].color.b);
//    ss << fmt::format("\tKd {} {} {}\n", cornellBox[2].vertices[0].color.r, cornellBox[2].vertices[0].color.g, cornellBox[2].vertices[0].color.b);
//    ss << "\tKs 1 1 1\n";
//    ss << "\tTf 1 1 1\n";
//    ss << "\tillum 0\n";
//    ss << "\tNs 50\n";
//    ss << "\tNi 1\n";
//
//
//    ss << fmt::format("\nnewmtl Green\n");
//    ss << fmt::format("\tKa {} {} {}\n", cornellBox[4].vertices[0].color.r, cornellBox[4].vertices[0].color.g, cornellBox[4].vertices[0].color.b);
//    ss << fmt::format("\tKd {} {} {}\n", cornellBox[4].vertices[0].color.r, cornellBox[4].vertices[0].color.g, cornellBox[4].vertices[0].color.b);
//    ss << "\tKs 1 1 1\n";
//    ss << "\tTf 1 1 1\n";
//    ss << "\tillum 0\n";
//    ss << "\tNs 50\n";
//    ss << "\tNi 1\n";
//
//    std::ofstream materialOut{"../../data/models/cornell_box.mtl"};
//    materialOut << ss.str();
//
//    ss.str("");
//    ss.clear();
//
//    ss << fmt::format("# cornell_box.obj\n\n");
//
//    ss << "mtllib cornell_box.mtl\n\n";
//
//    struct Meta{
//        std::string name;
//        std::string material;
//    };
//
//    std::map<int, Meta> objMetadata{};
//    objMetadata[0] = { "Light", "Light" };
//    objMetadata[1] = { "Ceiling", "White" };
//    objMetadata[2] = { "RightWall", "Red" };
//    objMetadata[3] = { "Floor", "White" };
//    objMetadata[4] = { "LeftWall", "Green" };
//    objMetadata[5] = { "TallBox", "White" };
//    objMetadata[6] = { "ShortBox", "White" };
//    objMetadata[7] = { "BackWall", "White" };
//
//    for(auto [index, metadata] : objMetadata) {
//        ss << std::format("o {}\n", metadata.name);
//        ss << std::format("usemtl {}\n", metadata.material);
//        const auto& obj = cornellBox[index];
//
//        for(auto vertices : obj.vertices) {
//            ss << std::format("v {} {} {}\n", vertices.position.x, vertices.position.y, vertices.position.z);
//        }
//
//        ss << "\n";
//        for(auto vertices : obj.vertices) {
//            ss << std::format("vn {:f} {:f} {:f}\n", vertices.normal.x, vertices.normal.y, vertices.normal.z);
//        }
//
//        ss << "\n";
//        for(auto vertices : obj.vertices) {
//            ss << std::format("vt {} {}\n", vertices.uv.s, vertices.uv.t);
//        }
//
//        ss << "\n";
//        const auto& indices = obj.indices;
//        for(int i = 0; i < indices.size(); i+= 3){
//            ss << std::format("f {} {} {}\n", indices[i] + 1, indices[i+1] + 1, indices[i+2] + 1);
//        }
//        ss << "\n";
//    }
//
//    std::ofstream objOut{"../../data/models/cornell_box.obj"};
//    objOut << ss.str();

//    glm::mat4 xform{1};
//    xform = glm::rotate(xform, glm::half_pi<float>(), {1, 0, 0});
//    auto plane = primitives::plane(1, 1, 1, 1, xform, glm::vec4(0), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
//
//    for(auto vertex :plane.vertices) {
//        fmt::print("position: [{:.1f}, {:.1f}, {:.1f}], uv: [{:.1f}, {:.1f}]\n",
//                   vertex.position.x, vertex.position.y, vertex.position.z,
//                   vertex.uv.x, vertex.uv.y);
//    }
//
//    auto vertex = plane.vertices.front();
////    fmt::print("position: {}\n", vertex.position.xyz());
//    fmt::print("normal: {}\n", vertex.normal.xyz());
//    fmt::print("tangent: {}\n", vertex.tangent.xyz());
//    fmt::print("bitangent: {}\n", vertex.bitangent.xyz());
//
//}
