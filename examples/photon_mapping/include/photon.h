#pragma once

#include <glm/glm.hpp>

struct Photon {
    glm::vec4 position;
    glm::vec4 power;
    glm::vec3 direction;
    int axis{-1};
    float delta2;
    char padding[12];
};

struct PhotonComp {
    glm::vec4 position;
    int axis;
    float delta2;
    int index;
};

PhotonComp convert(Photon photon, int index){
    return PhotonComp{photon.position, photon.axis, photon.delta2, index};
}

#define QUEUE_ENTRY_TYPE PhotonComp
#define QUEUE_ENTRY_COMP(a, b) (a.delta2 < b.delta2)