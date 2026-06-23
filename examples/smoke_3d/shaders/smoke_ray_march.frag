#version 460

#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_debug_printf: enable

struct Bounds {
    vec3 min;
    vec3 max;
};

layout(set = 0, binding = 0) uniform sampler3D tempAndDensityField;

layout(set = 1, binding = 0, scalar) buffer Cosntants {
    mat4 worldToVoxel;
    mat4 voxelToWorld;
    Bounds domain;
    Bounds emitterBounds;
    ivec3 resolution;
    vec3 up;
    float ambientTemp;
	float tempSum;
    float minValue;
    float maxValue;
    float tempFactor;
    float densityFactory;
    float smokeDecayFactor;
    float temperatureDecayFactor;
    uint numCells;
};

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
};

layout(location = 0) in struct {
    vec3 direction;
} fs_in;

layout(location = 0) out vec4 fragColor;

#define LPOS vec3( -0.5, 1.8, 0.9)
#define LCOL (vec3( 1.0 ))

bool testUnitCube(vec3 o, vec3 rd, out float tmin, out float tmax);
bool outOfBounds(vec3 pos);
void getParticipatingMedia(out float sigmaS, out float sigmaE, in vec3 pos);
vec3 evaluateLight(in vec3 pos, in vec3 normal);
vec3 evaluateLight(in vec3 pos);
float volumetricShadow(in vec3 from, in vec3 to);

float phaseFunction() {
    return 1.0/(4.0*3.14);
}


void main() {
    gl_FragDepth = 1;

    vec3 worldOrigin = (inverse(view) * vec4(0, 0, 0, 1)).xyz;
    vec3 worldDirection = normalize(fs_in.direction);

    vec3 rayOrigin = (worldToVoxel * vec4(worldOrigin, 1)).xyz;
    vec3 rayDirection = normalize((worldToVoxel * vec4(worldDirection, 0)).xyz);

    float tEnter;
    float tExit;
    vec4 scatterTransmission = vec4(0, 0, 0, 1);
    vec3 lightPos = vec3(-0.5, 1.8, 0.9);
    bool hasDensity = false;
    if(testUnitCube(rayOrigin, rayDirection, tEnter, tExit)) {
        ivec3 voxelDim = textureSize(tempAndDensityField, 0);
        int maxDim = max(voxelDim.x, max(voxelDim.y, voxelDim.z));
        float delta = 1.0 / float(maxDim);
        float t = max(tEnter, 0.0) + delta * 0.3;
        int maxSteps = max(1, int(ceil((tExit - t) / delta)) + 1);

        float sigmaS = 0.0;
        float sigmaE = 0.0;
        float transmittance = 1.0;
        vec3 scatteredLight = vec3(0.0, 0.0, 0.0);

        for(int i = 0; i < maxSteps; ++i, t += delta) {
            vec3 pos = rayOrigin + rayDirection * t;

            if(outOfBounds(pos)) break;

            getParticipatingMedia(sigmaS, sigmaE, pos);
            if(sigmaS > 0 && !hasDensity) hasDensity = true;
            vec3 S = evaluateLight(pos) * sigmaS * phaseFunction() * volumetricShadow(pos,lightPos);// incoming light
            vec3 Sint = (S - S * exp(-sigmaE * delta)) / sigmaE; // integrate along the current step segment
            scatteredLight += transmittance * Sint; // accumulate and also take into account the transmittance from previous steps

            // Evaluate transmittance to view independentely
            transmittance *= exp(-sigmaE * delta);
        }
        scatterTransmission = vec4(scatteredLight, transmittance);
    }
//    if(hasDensity) {
//        vec4 st = scatterTransmission;
//        debugPrintfEXT("st: [%f, %f, %f, %f]\n", st.x, st.y, st.z, st.w);
//    }
    fragColor = scatterTransmission;
}

bool testUnitCube(vec3 o, vec3 rd, out float tmin, out float tmax) {
    tmin = 0;
    tmax = 1e10;

    for(int i = 0; i < 3; ++i) {
        if(abs(rd[i]) < 1e-6){
            if(o[i] < 0 || o[i] > 1) return false;
        }else {
            float invRd = 1.0/rd[i];
            float t1 = (0 - o[i]) * invRd;
            float t2 = (1 - o[i]) * invRd;

            if(t1 > t2) {
                float temp = t1;
                t1 = t2;
                t2 = temp;
            }
            tmin = max(tmin, t1);
            tmax = min(tmax, t2);
            if(tmin > tmax) return false;
        }
    }
    return tmax >= max(tmin, 0.0);
}

bool outOfBounds(vec3 pos) {
    bvec3 near = lessThan(pos, vec3(0));
    bvec3 far = greaterThanEqual(pos, vec3(1));

    return any(near) || any(far);
}



void getParticipatingMedia(out float sigmaS, out float sigmaE, in vec3 pos) {
    vec2 tempAndDensity = texture(tempAndDensityField, pos).xy;
    float density = max(tempAndDensity.y, 0.0);
    sigmaS = density * 10;

    const float sigmaA = density * 200;
    sigmaE = max(0.000000001, sigmaA + sigmaS); // to avoid division by zero extinction

}

vec3 evaluateLight(in vec3 pos) {
    vec3 lightPos = LPOS;
    vec3 lightCol = LCOL;
    vec3 L = lightPos-pos;
    return lightCol * 1.0/dot(L,L);
}

vec3 evaluateLight(in vec3 pos, in vec3 normal) {
    vec3 lightPos = LPOS;
    vec3 L = lightPos-pos;
    float distanceToL = length(L);
    vec3 Lnorm = L/distanceToL;
    return max(0.0,dot(normal,Lnorm)) * evaluateLight(pos);
}

float volumetricShadow(in vec3 from, in vec3 to) {
    float shadow = 1.0;
    float sigmaS = 0.0;
    float sigmaE = 0.0;

    ivec3 voxelDim = textureSize(tempAndDensityField, 0);
    int maxDim = max(voxelDim.x, max(voxelDim.y, voxelDim.z));

    float tEnter, tExit;
    vec3 dir = normalize(to - from);
    testUnitCube(from, dir, tEnter, tExit);
    int numSteps = 4;
    float delta = (tExit - tEnter)/float(numSteps);

    float t = 0;
    for(int i = 0; i < numSteps; ++i) {
        vec3 pos = from + dir * t;
        getParticipatingMedia(sigmaS, sigmaE, pos);
        shadow *= exp(-sigmaE * delta);
        t += delta;
    }
    return shadow;
}
