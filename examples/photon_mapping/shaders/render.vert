#version 460 core

#include "photon.glsl"

layout(set = 0, binding = 1, std430) buffer GENERATED_PHOTONS {
    Photon photons[];
};

layout(set = 0, binding = 3) buffer TreeIndex {
    int tree[];
};


layout(set = 1, binding = 0) buffer DebugData {
    mat4 transform;

    vec3 hitPosition;
    float radius;

    vec3 target;
    int mode;

    vec3 cameraPosition;
    int meshId;

    vec3 pointColor;
    int numNeighboursFound;

    float pointSize;
    int searchMode;
    int numNeighbours;
};

layout(set = 1, binding = 1) buffer SearchResults {
    int searchResults[];
};

layout(push_constant) uniform MVP {
    mat4 model;
    mat4 view;
    mat4 projection;
};

layout(location = 0) out vec3 vColor;

bool showPhoton() {
    for(int i = 0; i < numNeighbours; i++){
        if(searchResults[i] == gl_InstanceIndex){
            return true;
        }
    }
    return false;
}

void main(){
     if(searchMode == 1 && showPhoton()) {
        Photon photon = photons[gl_InstanceIndex];
        gl_PointSize = pointSize;
        vColor = pointColor;
        gl_Position = projection * view * photon.position;
    }
     else {
         Photon photon = photons[gl_InstanceIndex];
         gl_PointSize = pointSize;
         vColor = photon.power.rgb;
         gl_Position = projection * view * photon.position;
     }
}