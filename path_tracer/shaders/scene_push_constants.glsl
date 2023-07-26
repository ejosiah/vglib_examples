#ifndef SCENE_PUSH_CONSTANTS
#define SCENE_PUSH_CONSTANTS

layout(push_constant) uniform SceneConstants {
    uint maxBounces;
    uint frame;
    uint currentSample;
    uint numSamples;
    int numLights;
    int adaptiveSampling;
    float worldRadius;
    float pMarginalIntegral;
    uint mask;
    float exposure;
    float skyIntensity;
    float envMapIntensity;
    int planeId;
};

#endif // SCENE_PUSH_CONSTANTS