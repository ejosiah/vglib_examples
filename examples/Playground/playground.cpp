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
    const vec3 position{3, 0, 1};
    const vec3 camera_position{0.0421436876, 0.679705858, 2.06078792};
    const vec3 camera_direction{0.0418740623, -0.00889993645, -0.99908328};
    vec3 cam_to_position = position - camera_position;

    auto sign_ =  sign(dot(camera_direction, cam_to_position));
    auto distance_ = distance(camera_position, position);
    auto res = sign_ * distance_;
    fmt::print("{}\n", res);
}