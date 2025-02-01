#ifndef SHADOW_MAP_GLSL
#define SHADOW_MAP_GLSL

float shadowCalculation(sampler2DArray shadowMap, vec4 lightSpacePos, uint cascadeIndex){
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    if(projCoords.z > 1.0){
        return 0.0;
    }
    float closestDepth = texture(shadowMap, vec3(projCoords.xy, cascadeIndex)).r;
    float currentDepth = projCoords.z;
    float shadow = currentDepth > closestDepth ? 1.0 : 0.0;
    return shadow;
}

float pcfFilteredShadow(sampler2DArray shadowMap, vec4 lightSpacePos, uint cascadeIndex){
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    if(projCoords.z > 1.0){
        return 0.0;
    }
    float shadow = 0.0f;
    float currentDepth = projCoords.z;
    vec2 texelSize = 1.0/textureSize(shadowMap, 0).xy;
    for(int x = -1; x <= 1; x++){
        for(int y = -1; y <= 1; y++){
            vec3 uvw = vec3(projCoords.xy + vec2(x, y) * texelSize, cascadeIndex);
            float pcfDepth = texture(shadowMap, uvw).r;
            shadow += currentDepth > pcfDepth ? 1.0 : 0.0;
        }
    }
    return shadow/9.0;
}

#endif // SHADOW_MAP_GLSL