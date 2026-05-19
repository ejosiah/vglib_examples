/*
Source: Balazs Jako and Balazs Toth, "Fast Hydraulic and Thermal Erosion on the GPU" (CESCG 2011).
URL: https://old.cescg.org/CESCG-2011/papers/TUBudapest-Jako-Balazs.pdf
Shared constants, bindings, and accessors for erosion passes implementing equations (1)-(16).
*/

#ifndef EROSION_CONSTANTS_GLSL
#define EROSION_CONSTANTS_GLSL

#extension GL_EXT_nonuniform_qualifier : enable

layout(push_constant) uniform Constants {
    ivec2 terrainSize;
    float timeStep; // dt
    float rainScale; // Kr
    float pipeArea; // A
    float inversePipeLength; // 1/l
    float gravity; // g
    float sedimentCapacity; // Kc
    float thermalErosionRate; // Kt
    float soilSuspensionRate; // Ks
    float sedimentDepositionRate; // Kd
    float evaporationRate; // Ke
    float sedimentSofteningRate; // Kh
    float minimumHardness; // Rmin
    float maximalErosionDepth; // Kd_max
    float talusAngleTangentCoeff; // Ka
    float talusAngleTangentBias; // Ki
    uint iteration;
    uint maxIterations;
    uint terrainHeightTextureIndex;
    uint waterHeightTextureIndex;
    uint sedimentAmountTextureIndex;
    uint fluxTextureIndex;
    uint velocityFieldTextureIndex;
    uint rainTextureIndex;
    uint localHardnessCoefTextureIndex;
    uint worksheetTextureIndex;
    uint terrainHeightImageIndex;
    uint waterHeightImageIndex;
    uint sedimentAmountImageIndex;
    uint fluxImageIndex;
    uint velocityFieldImageIndex;
    uint rainImageIndex;
    uint localHardnessCoefImageIndex;
    uint worksheetImageIndex;
};

layout(set = 0, binding = 10) uniform sampler2D global_textures[];
layout(set = 0, binding = 11, r32f) uniform image2D global_r32_images[];
layout(set = 0, binding = 11, rg32f) uniform image2D global_rg32_images[];
layout(set = 0, binding = 11, rgba32f) uniform image2D global_rgba32_images[];

#define terrain_height_texture global_textures[nonuniformEXT(terrainHeightTextureIndex)]
#define water_height_texture global_textures[nonuniformEXT(waterHeightTextureIndex)]
#define sediment_amount_texture global_textures[nonuniformEXT(sedimentAmountTextureIndex)]
#define flux_texture global_textures[nonuniformEXT(fluxTextureIndex)]
#define velocity_field_texture global_textures[nonuniformEXT(velocityFieldTextureIndex)]
#define rain_texture global_textures[nonuniformEXT(rainTextureIndex)]
#define local_hardness_coef_texture global_textures[nonuniformEXT(localHardnessCoefTextureIndex)]
#define worksheet_texture global_textures[nonuniformEXT(worksheetTextureIndex)]

#define terrain_height_image global_r32_images[nonuniformEXT(terrainHeightImageIndex)]
#define water_height_image global_r32_images[nonuniformEXT(waterHeightImageIndex)]
#define sediment_amount_image global_r32_images[nonuniformEXT(sedimentAmountImageIndex)]
#define flux_image global_rgba32_images[nonuniformEXT(fluxImageIndex)]
#define velocity_field_image global_rg32_images[nonuniformEXT(velocityFieldImageIndex)]
#define rain_image global_r32_images[nonuniformEXT(rainImageIndex)]
#define local_hardness_coef_image global_r32_images[nonuniformEXT(localHardnessCoefImageIndex)]
#define worksheet_image global_rgba32_images[nonuniformEXT(worksheetImageIndex)]

bool inTerrain(ivec2 loc) {
    return all(greaterThanEqual(loc, ivec2(0))) && all(lessThan(loc, terrainSize));
}

float getTerrainHeight(ivec2 loc) {
    return texelFetch(terrain_height_texture, loc, 0).r;
}

float getWaterHeight(ivec2 loc) {
    return texelFetch(water_height_texture, loc, 0).r;
}

float getSedimentAmount(ivec2 loc) {
    return texelFetch(sediment_amount_texture, loc, 0).r;
}

float getSedimentAmountBiLinear(vec2 loc) {
    vec2 uv = (clamp(loc, vec2(0.0), vec2(terrainSize - ivec2(1))) + 0.5) / vec2(terrainSize);
    return texture(sediment_amount_texture, uv).r;
}

float getLocalHardnessCoef(ivec2 loc) {
    return texelFetch(local_hardness_coef_texture, loc, 0).r;
}

vec2 getVelocityField(ivec2 loc) {
    return texelFetch(velocity_field_texture, loc, 0).rg;
}

vec3 getNormal(ivec2 loc) {
    ivec2 maxLoc = terrainSize - ivec2(1);
    float heightLeft = getTerrainHeight(clamp(loc + ivec2(-1, 0), ivec2(0), maxLoc));
    float heightRight = getTerrainHeight(clamp(loc + ivec2(1, 0), ivec2(0), maxLoc));
    float heightDown = getTerrainHeight(clamp(loc + ivec2(0, -1), ivec2(0), maxLoc));
    float heightUp = getTerrainHeight(clamp(loc + ivec2(0, 1), ivec2(0), maxLoc));

    return normalize(vec3(heightLeft - heightRight, 2.0, heightDown - heightUp));
}

float getCarryingCapacity(ivec2 loc) {
    return texelFetch(worksheet_texture, loc, 0).y;
}

float getAdvectedSediment(ivec2 loc) {
    return texelFetch(worksheet_texture, loc, 0).x;
}

vec4 getOutflow(ivec2 loc) {
    return texelFetch(flux_texture, loc, 0);
}

float getOutflow(ivec3 loc) {
    return texelFetch(flux_texture, loc.xy, 0)[loc.z];
}

void setWaterHeight(ivec2 loc, float value) {
    imageStore(water_height_image, loc, vec4(value));
}

void setTerrainHeight(ivec2 loc, float value) {
    imageStore(terrain_height_image, loc, vec4(value));
}

void setSedimentAmount(ivec2 loc, float value) {
    imageStore(sediment_amount_image, loc, vec4(value));
}

void setLocalHardnessCoef(ivec2 loc, float value) {
    imageStore(local_hardness_coef_image, loc, vec4(value));
}

void setOutflow(ivec2 loc, vec4 value) {
    imageStore(flux_image, loc, value);
}

void setWaterVolumeChange(ivec2 loc, float DeltaV) {
    imageStore(worksheet_image, loc, vec4(DeltaV, 0.0, 0.0, 0.0));
}

void setCarryingCapacity(ivec2 loc, float C) {
    vec4 worksheet = texelFetch(worksheet_texture, loc, 0);
    imageStore(worksheet_image, loc, vec4(worksheet.x, C, worksheet.zw));
}

void setSedimentExchange(ivec2 loc, float DeltaS, float mode) {
    vec4 worksheet = texelFetch(worksheet_texture, loc, 0);
    imageStore(worksheet_image, loc, vec4(worksheet.x, worksheet.y, DeltaS, mode));
}

void setAdvectedSediment(ivec2 loc, float st_dt) {
    vec4 worksheet = texelFetch(worksheet_texture, loc, 0);
    imageStore(worksheet_image, loc, vec4(st_dt, worksheet.yzw));
}

#endif
