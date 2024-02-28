#ifndef SAMPLING_GLSL
#define SAMPLING_GLSL

float RadicalInverse_VdC(uint bits){
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

vec2 hammersley(uint i, uint N){
    return vec2(float(i)/float(N), RadicalInverse_VdC(i));
}

vec3 cosineSampleHemisphere(vec2 xi){
    const float PI = 3.1415;
    // Uniformly sample disk.
    const float r   = sqrt(xi.x);
    const float phi = 2.0f * PI * xi.y;

    vec2 p;
    p.x = r * cos(phi);
    p.y = r * sin(phi);

    return vec3(p, sqrt(max(0.0f, 1.0f - dot(p, p))));
}

#endif // SAMPLING_GLSL