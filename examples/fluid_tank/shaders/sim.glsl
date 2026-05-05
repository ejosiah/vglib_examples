#ifndef FLUID_TANK_SIM_GLSL
#define FLUID_TANK_SIM_GLSL

layout(set = 0, binding = 0) uniform Simulation {
    ivec4 gridSize;
    vec4 invGridSizeDt;
    vec4 spherePosRadius;
    vec4 sphereVelocity;
    vec4 fluidParams;
} sim;

layout(set = 1, binding = 0) uniform sampler3D velocityTex;
layout(set = 1, binding = 1) uniform sampler3D sourceTex;
layout(set = 1, binding = 2) uniform sampler3D pressureTex;
layout(set = 1, binding = 3) uniform sampler3D divergenceTex;
layout(set = 1, binding = 4) uniform sampler3D obstacleTex;

bool outOfBounds(ivec3 id) {
    return any(lessThan(id, ivec3(0))) || any(greaterThanEqual(id, sim.gridSize.xyz));
}

vec3 uvwFromId(ivec3 id) {
    return (vec3(id) + 0.5) * sim.invGridSizeDt.xyz;
}

float obstacleAt(ivec3 id) {
    if (outOfBounds(id)) {
        return 1.0;
    }
    return texture(obstacleTex, uvwFromId(id)).r;
}

float scalarAt(sampler3D texRef, ivec3 id) {
    if (outOfBounds(id)) {
        return 0.0;
    }
    return texture(texRef, uvwFromId(id)).r;
}

vec3 velocityAt(ivec3 id) {
    if (outOfBounds(id)) {
        return vec3(0.0);
    }
    return texture(velocityTex, uvwFromId(id)).xyz;
}

#endif
