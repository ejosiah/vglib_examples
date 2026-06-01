#ifndef TRANSFORMS_GLSL
#define TRANSFORMS_GLSL

vec2 clip_space_to_pixel(vec4 positionCS, vec2 screenSize) {
    vec2 p = positionCS.xy / positionCS.w;
    p.xy = (p.xy * 0.5 + 0.5);
    p.xy *= screenSize.xy;
    return p.xy;
}

// Planet space to normalized planet space
vec3 evaluate_normalized_planet_space(vec3 positionPS, float planetRadius) {
    vec3 posNPS = positionPS / planetRadius;
    return posNPS.y == 1.0 ? vec3(0, 1, 0) : posNPS / length(posNPS);
}

dvec3 evaluate_normalized_planet_space(dvec3 positionPS, double planetRadius) {
    dvec3 posNPS = positionPS / planetRadius;
    return posNPS.y == 1.0 ? dvec3(0, 1, 0) : posNPS * inversesqrt(dot(posNPS, posNPS));
}

#endif // TRANSFORMS_GLSL
