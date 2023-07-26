#version 450
#define PI 3.1415926535897932384626433832795

#include "terrain_ubo.glsl"
#include "terrain_lod.glsl"

layout (vertices = 4) out;

layout(set = 0, binding = 1) uniform sampler2D displacementMap;

layout(location = 0) in vec2 uvIn[];
layout(location = 1) in vec3 normal_in[];

layout(location = 0) out vec2 uvOut[];
layout(location = 1) out vec3 normal_out[];
layout(location = 2) out vec3 color_out[];

float remap(float x, float a, float b, float c, float d){
    float t = clamp((x - a)/(b - a), 0, 1);
    return mix(c, d, t);
}

void main(){
    barrier();

    if(gl_InvocationID == 0){
        if (lod == 0){
            float tessLevel = maxTessLevel * 0.25;
            gl_TessLevelOuter[0] = tessLevel;
            gl_TessLevelOuter[1] = tessLevel;
            gl_TessLevelOuter[2] = tessLevel;
            gl_TessLevelOuter[3] = tessLevel;
            gl_TessLevelInner[0] = tessLevel;
            gl_TessLevelInner[1] = tessLevel;
        } else {
            LodParams lodParams;
            lodParams.modelView = view * model;
            lodParams.projection = projection;
            lodParams.positions[0] = gl_in[0].gl_Position;
            lodParams.positions[1] = gl_in[1].gl_Position;
            lodParams.positions[2] = gl_in[2].gl_Position;
            lodParams.positions[3] = gl_in[3].gl_Position;
            lodParams.displacement[0] = remap(texture(displacementMap, uvIn[0]).x, 0, 1, minZ, maxZ) * heightScale;
            lodParams.displacement[1] = remap(texture(displacementMap, uvIn[1]).x, 0, 1, minZ, maxZ) * heightScale;
            lodParams.displacement[2] = remap(texture(displacementMap, uvIn[2]).x, 0, 1, minZ, maxZ) * heightScale;
            lodParams.displacement[3] = remap(texture(displacementMap, uvIn[3]).x, 0, 1, minZ, maxZ) * heightScale;
            lodParams.viewport = viewportSize;
            lodParams.minDepth = lodMinDepth;
            lodParams.maxDepth = lodMaxDepth;
            lodParams.minTessLevel = minTessLevel;
            lodParams.maxTessLevel = maxTessLevel;
            lodParams.targetTriangleWidth = lodTargetTriangleWidth;
            lodParams.lodType = lodStrategy;

            terrainLOD(lodParams, gl_TessLevelOuter, gl_TessLevelInner);
        }
    }

    float level = 1 - ((gl_TessLevelOuter[0] - minTessLevel)/(maxTessLevel - minTessLevel));
    float freq = PI * 0.5;
    color_out[gl_InvocationID] = vec3((freq * level), sin(freq * 2 * level), cos(freq * level));

    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

    uvOut[gl_InvocationID] = uvIn[gl_InvocationID];
    normal_out[gl_InvocationID] = normal_in[gl_InvocationID];
}