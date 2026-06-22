#pragma once

#include <glm/glm.hpp>
#include <limits>

struct BoundingBox {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};
};

inline BoundingBox boundingBox(glm::vec3 min, glm::vec3 max) {
    return { min, max };
}

inline bool empty(BoundingBox box) {
    return glm::any(glm::greaterThan(box.min, box.max));
}

inline BoundingBox expand(BoundingBox box, glm::vec3 point) {
    box.min = glm::min(box.min, point);
    box.max = glm::max(box.max, point);
    return box;
}

inline BoundingBox expand(BoundingBox box, BoundingBox other) {
    if(empty(other)) {
        return box;
    }
    box.min = glm::min(box.min, other.min);
    box.max = glm::max(box.max, other.max);
    return box;
}

inline BoundingBox inflate(BoundingBox box, glm::vec3 delta) {
    box.min -= delta;
    box.max += delta;
    return box;
}

inline BoundingBox inflate(BoundingBox box, float delta) {
    return inflate(box, glm::vec3(delta));
}

inline glm::vec3 remap(glm::vec3 x, BoundingBox oldBox, BoundingBox newBox) {
    return glm::mix(newBox.min, newBox.max, (x - oldBox.min) / (oldBox.max - oldBox.min));
}

inline glm::vec3 diagonal(BoundingBox box) {
    return box.max - box.min;
}

inline float width(BoundingBox box) {
    return diagonal(box).x;
}

inline float height(BoundingBox box) {
    return diagonal(box).y;
}

inline float depth(BoundingBox box) {
    return diagonal(box).z;
}

inline glm::vec3 midPoint(BoundingBox box) {
    return (box.min + box.max) * 0.5f;
}

inline glm::vec3 closestPoint(BoundingBox box, glm::vec3 point) {
    return glm::clamp(point, box.min, box.max);
}

inline glm::vec3 closedPoint(BoundingBox box, glm::vec3 point) {
    return closestPoint(box, point);
}

inline bool contains(BoundingBox box, glm::vec3 point) {
    return glm::all(glm::greaterThanEqual(point, box.min))
        && glm::all(glm::lessThanEqual(point, box.max));
}

inline bool intersects(BoundingBox a, BoundingBox b) {
    return glm::all(glm::greaterThanEqual(a.max, b.min))
        && glm::all(glm::greaterThanEqual(b.max, a.min));
}

inline glm::mat4 toLocalSpace(const BoundingBox& box) {
    auto dim = box.max - box.min;
    glm::mat4 transform = glm::scale(glm::mat4{1}, glm::vec3(1.f/dim));
    transform = glm::translate(transform, -box.min);

    return transform;
}