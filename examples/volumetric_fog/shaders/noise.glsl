#ifndef NOISE_GLSL
#define NOISE_GLSL

#ifndef BIND_LESS_ENABLED
#define BLUE_NOISE_TEXTURE blueNoise
#endif

float triangularNoise( float noise0, float noise1 ) {
    return noise0 + noise1 - 1.0f;
}

float generateNoise(vec2 uv, int frame, float scale) {
    // Read blue noise from texture
    vec2 blue_noise = texture(BLUE_NOISE_TEXTURE, uv ).rg;
    const float k_golden_ratio_conjugate = 0.61803398875;
    float blue_noise0 = fract(blue_noise.r + float(frame % 256) * k_golden_ratio_conjugate);
    float blue_noise1 = fract(blue_noise.g + float(frame % 256) * k_golden_ratio_conjugate);

    return triangularNoise(blue_noise0, blue_noise1) * scale;
}

float generateNoise(vec2 pixel, int frame) {
    return generateNoise(pixel, frame, 1.0);
}

#endif // NOISE_GLSL