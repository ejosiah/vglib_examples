#include <glm/gtc/matrix_transform.hpp>
#include <fmt/format.h>
#include <glm/gtc/matrix_access.hpp>

#include <algorithm>
#include <vector>
#include <glm/glm.hpp>

using namespace glm;

float densityFunction(vec3 x) {
    return -x.y;
}

int main() {
    uvec3 gl_WorkGroupSize(32, 1, 32);

//    vec3 world_position{2, 0, 2.5};
    vec3 world_position{2.0000000000, -1.0000000000, 2.5000000000};
//    vec3 world_position{0};
    const vec3 origin = world_position - vec3(0.5, 0.5, 0.5);

    std::vector<float> densities;

    float scale = 1.f/32.f;
    for(uint wz = 0; wz < gl_WorkGroupSize.z; ++wz) {
        for(uint wy = 0; wy < gl_WorkGroupSize.y; ++wy) {
            for(uint wx = 0; wx < gl_WorkGroupSize.x; ++wx) {
                uvec3 gl_LocalInvocationID(wx, wy, wz);

                uint layers = 33;
                for(uint y = 0; y < layers; ++y) {
                    ivec3 location = ivec3(gl_LocalInvocationID.x, y, gl_LocalInvocationID.z);
                    vec3 position = origin + vec3(location) * scale;
                    float density = densityFunction(position);
                    fmt::print("{}\n", density);
                    densities.push_back(density);

                    location = ivec3(gl_WorkGroupSize.x, y, gl_LocalInvocationID.z);
                    position = origin + vec3(location) * scale;
                    density = densityFunction(position);
                    fmt::print("{}\n", density);
                    densities.push_back(density);

                    location = ivec3(gl_LocalInvocationID.x, y, gl_WorkGroupSize.z);
                    position = origin + vec3(location) * scale;
                    density = densityFunction(position);
                    fmt::print("{}\n", density);
                    densities.push_back(density);

                    location = ivec3(gl_WorkGroupSize.x, y, gl_WorkGroupSize.z);
                    position = origin + vec3(location) * scale;
                    density = densityFunction(position);
                    fmt::print("{}\n", density);
                    densities.push_back(density);
                }
            }
        }
    }

    bool belowGround  = std::all_of(densities.begin(), densities.end(), [](const auto& d){ return d > 0; });
    bool aboveGround  = std::all_of(densities.begin(), densities.end(), [](const auto& d){ return d < 0; });

    if(belowGround) {
        fmt::print("block is below ground");
    }else if(aboveGround) {
        fmt::print("block is above ground");
    }else {
        fmt::print("block is surface boundary");
    }


}