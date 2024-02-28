#ifndef PHOTON_GLSL
#define PHOTON_GLSL

struct Photon {
    vec4 position;
    vec4 power;
    vec3 direction;
    int axis;
    float delta2;
};

struct PhotonComp {
    vec4 position;
    int axis;
    float delta2;
    int index;
};

PhotonComp convert(Photon photon, int index){
    return PhotonComp(photon.position, photon.axis, photon.delta2, index);
}

#define QUEUE_ENTRY_TYPE PhotonComp
#define QUEUE_ENTRY_COMP(a, b) (a.delta2 < b.delta2)


#endif // PHOTON_GLSL