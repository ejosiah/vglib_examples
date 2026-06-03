#ifndef WATER_UTILS_GLSL
#define WATER_UTILS_GLSL

#include "../common.glsl"

// Water constants
#define ONE_OVER_SQRT2 0.70710678118
#define EARTH_GRAVITY 9.81
#define PHILLIPS_AMPLITUDE_SCALAR 0.2
#define NOISE_FUNCTION_OFFSET 64u

vec2 orientation_to_direction(float orientation)
{
    return vec2(cos(orientation), sin(orientation));
}

float phillips_spectrum(vec2 k, vec2 w, float V, float directionDampener, float patchSize)
{
    float kk = k.x * k.x + k.y * k.y;
    float result = 0.0;
    if (kk != 0.0)
    {
        float L = (V * V) / EARTH_GRAVITY;
        // To avoid _any_ directional bias when there is no wind we lerp towards 0.5f
        float wk = mix(dot(normalize(k), w), 0.5, directionDampener);
        float phillips = (exp(-1.0f / (kk * L * L)) / (kk * kk)) * (wk * wk);
        result = phillips * (wk < 0.0f ? directionDampener : 1.0);
    }
    return PHILLIPS_AMPLITUDE_SCALAR * result / (patchSize * patchSize);
}

vec2 complex_conjugate(vec2 a)
{
    return vec2(a.x, -a.y);
}

vec2 complex_exp(float arg)
{
    return vec2(cos(arg), sin(arg));
}

vec2 complex_mult(vec2 a, vec2 b)
{
    return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

float frquency_phase(vec2 complex)
{
    return atan(complex.y / complex.x);
}

float frequency_amplitude(vec2 complex)
{
    return sqrt(complex.x * complex.x + complex.y * complex.y);
}

vec3 ShuffleDisplacement(vec3 displacement)
{
    return vec3(-displacement.y, displacement.x, -displacement.z);
}

void EvaluateDisplacedPoints(vec3 center, vec3 right, vec3 up, float pixelSize, out vec3 p0, out vec3 p1, out vec3 p2)
{
    p0 = vec3(center.x, center.y, center.z);
    p1 = vec3(right.x, right.y, right.z) + vec3(pixelSize, 0, 0);
    p2 = vec3(up.x, up.y, up.z) + vec3(0, 0, pixelSize);
}

vec3 evaluate_surface_gradients(vec3 p0, vec3 p1, vec3 p2)
{
    vec3 v0 = p1 - p0;
    vec3 v1 = p2 - p0;
    return surface_gradient_from_perturbed_normal(vec3(0, 1, 0), normalize(cross(v1, v0)));
}


#endif // WATER_UTILS_GLSL
