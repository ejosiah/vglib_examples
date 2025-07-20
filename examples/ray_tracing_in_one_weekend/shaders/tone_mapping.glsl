#ifndef TONE_MAPPING_GLSL
#define TONE_MAPPING_GLSL

#define Clamp 0
#define Reinhard 1
#define Uncharted2 2
#define ACES 3
#define Hejl 4

vec3 no_tone_mapping(vec3 color) {
    return clamp(color, 0.0, 1.0);
}

vec3 tone_map_reinhard(vec3 x) {
    return x / (x + 1.0);
}


vec3 tone_map_uncharted2(vec3 x) {
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    float W = 11.2; // white point

    vec3 mapped = ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F)) - E/F;
    float white_scale = ((W*(A*W+C*B)+D*E)/(W*(A*W+B)+D*F)) - E/F;
    return mapped / white_scale;
}

vec3 tone_map_aces(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x*(a*x + b)) / (x*(c*x + d) + e), 0.0, 1.0);
}

vec3 tone_map_hejl(vec3 x) {
    x = max(vec3(0.0), x - 0.004);
    return (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
}

vec3 tone_map(vec3 color, int method) {
    switch (method) {
        case Reinhard: return tone_map_reinhard(color);
        case Uncharted2: return tone_map_uncharted2(color);
        case ACES: return tone_map_aces(color);
        case Hejl: return tone_map_hejl(color);
        default: return no_tone_mapping(color);
    }
}

#endif // TONE_MAPPING_GLSL