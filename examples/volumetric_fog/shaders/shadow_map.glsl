#ifndef SHADOW_MAP_GLSL
#define SHADOW_MAP_GLSL

#ifndef SHADOW_MAP_SET
#define SHADOW_MAP_SET 0
#endif // SHADOW_MAP_SET

#ifdef BIND_LESS_ENABLED

#define SHADOW_TEXTURE_ID 1
#define SHADOW_MAP gTextures[SHADOW_TEXTURE_ID]

#else
#define SHADOW_MAP shadowMap
layout(set = 5, binding = 0) uniform sampler2D shadowMap;
#endif

float shadowCalculation(vec4 lightSpacePos){
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    if(projCoords.z > 1.0){
        return 0.0;
    }
    float closestDepth = texture(SHADOW_MAP, projCoords.xy).r;
    float currentDepth = projCoords.z;
    float shadow = currentDepth > closestDepth ? 1.0 : 0.0;
    return shadow;
}

float pcfFilteredShadow(vec4 lightSpacePos){
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    if(projCoords.z > 1.0){
        return 0.0;
    }
    float shadow = 0.0f;
    float currentDepth = projCoords.z;
    vec2 texelSize = 1.0/textureSize(SHADOW_MAP, 0);
    for(int x = -1; x <= 1; x++){
        for(int y = -1; y <= 1; y++){
            float pcfDepth = texture(SHADOW_MAP, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth > pcfDepth ? 1.0 : 0.0;
        }
    }
    return shadow/9.0;
}

#endif // SHADOW_MAP_GLSL