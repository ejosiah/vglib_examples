#ifndef LARGE_LEB_COMMON_FUNC_GLSL
#define LARGE_LEB_COMMON_FUNC_GLSL

#include "types.glsl"

// Generic constants
#define PI 3.14159265358979323846
#define HALF_PI (PI / 2.0)
#define TWO_PI (PI * 2.0)
#define FLT_EPSILON 1.192092896e-07
#define INV_PI (1.0 /  PI)
#define FLT_MIN 1.175494351e-38
#define FLT_MAX 3.402823466e+38
#define PHI 1.61803398874989484820459
#define uvec32_MAX 0xffffffffU

// Set of materials
#define UNUSED_MATERIAL 0
#define EARTH_MATERIAL 1
#define MOON_MATERIAL 2

// Unfortunately this breaks on vulkan, so I am doing it manually.
uvec2 reversebits_uvec2(const uvec2 inputVal)
{
uvec2 x = inputVal;
x = (((x & 0xaaaaaaaau) >> 1) | ((x & 0x55555555u) << 1));
x = (((x & 0xccccccccu) >> 2) | ((x & 0x33333333u) << 2));
x = (((x & 0xf0f0f0f0u) >> 4) | ((x & 0x0f0f0f0fu) << 4));
x = (((x & 0xff00ff00u) >> 8) | ((x & 0x00ff00ffu) << 8));
return((x >> 16) | (x << 16));
}

// http://www.dspguide.com/ch2/6.htm
float gaussian_distribution(float u, float v)
{
    return sqrt(-2.0 * log(max(u, 1e-6f))) * cos(PI * v);
}

float pick_closest(float p, float n, float s)
{
    float nC = n + s;
    float distX0 = p - n;
    float distX1 = p - nC;
    return abs(distX0) < abs(distX1) ? n : nC;
}

vec2 compare_and_pick(vec2 p, vec2 n, float s)
{
    return vec2(pick_closest(p.x, n.x, s), pick_closest(p.y, n.y, s));
}

#ifdef ENABLE_FRAGMENT_DERIVATIVES
void evaluate_frac_derivatives(vec2 bandUV, out vec2 uvDDX, out vec2 uvDDY)
{
    // Evaluate the derivatives
    vec2 ddxUV = dFdx(bandUV);
    vec2 uvX = bandUV + ddxUV;
    uvX = compare_and_pick(bandUV, uvX, 1.0);
    uvX = compare_and_pick(bandUV, uvX, -1.0);
    uvDDX = bandUV - uvX;

    vec2 ddyUV = dFdy(bandUV);
    vec2 uvY = bandUV + ddyUV;
    uvY = compare_and_pick(bandUV, uvY, 1.0);
    uvY = compare_and_pick(bandUV, uvY, -1.0);
    uvDDY = bandUV - uvY;
}
#endif

ivec2 repeat_coord(ivec2 tap, uint width, uint height)
{
    return ivec2(uint(tap.x) % width, uint(tap.y) % height);
}

vec3 surface_gradient_from_perturbed_normal(vec3 nrmVertexNormal, vec3 v)
{
    vec3 n = nrmVertexNormal;
    float s = 1.0 / max(FLT_EPSILON, abs(dot(n, v)));
    return s * (dot(n, v) * n - v);
}

uvec4 hash_function_uvec4(uvec3 coord)
{
    uvec4 x = coord.xyzz;
    x = ((x >> 16u) ^ x.yzxy) * 0x45d9f3bu;
    x = ((x >> 16u) ^ x.yzxz) * 0x45d9f3bu;
    x = ((x >> 16u) ^ x.yzxx) * 0x45d9f3bu;
    return x;
}

vec4 hash_function_vec4(uvec3 p)
{
    return hash_function_uvec4(p) / float(0xffffffffU);
}

float sanitize_normal(inout vec3 normalWS, vec3 viewWS, vec3 positionRWS)
{
    // Check if we need to patch the normal
    float NdotV = dot(normalWS, viewWS);
    // the geometry normal is probably flipped, reverse it
    if (NdotV < 0.0)
    {
        normalWS =  reflect(normalWS, viewWS);
        NdotV = -NdotV;
    }
    return NdotV;
}

// - r0: ray origin
// - rd: normalized ray direction
// - s0: sphere center
// - sR: sphere radius
// - Returns distance from r0 to first intersecion with sphere,
//   or -1.0 if no intersection.
float ray_sphere_intersect_nearest(vec3 r0, vec3 rd, vec3 s0, float sR)
{
    float a = dot(rd, rd);
    vec3 s0_r0 = r0 - s0;
    float b = 2.0 * dot(rd, s0_r0);
    float c = dot(s0_r0, s0_r0) - (sR * sR);
    float delta = b * b - 4.0*a*c;
    if (delta < 0.0 || a == 0.0)
    return -1.0;
    float sol0 = (-b - sqrt(delta)) / (2.0*a);
    float sol1 = (-b + sqrt(delta)) / (2.0*a);
    if (sol0 < 0.0 && sol1 < 0.0)
    return -1.0;
    if (sol0 < 0.0)
    return max(0.0, sol1);
    else if (sol1 < 0.0)
    return max(0.0, sol0);
    return max(0.0, min(sol0, sol1));
}

double ray_sphere_intersect_nearest_d(dvec3 r0, dvec3 rd, dvec3 s0, double sR)
{
    double a = dot(rd, rd);
    dvec3 s0_r0 = r0 - s0;
    double b = 2.0 * dot(rd, s0_r0);
    double c = dot(s0_r0, s0_r0) - (sR * sR);
    double delta = b * b - 4.0*a*c;
    if (delta < 0.0 || a == 0.0)
    return -1.0;
    double sol0 = (-b - sqrt(delta)) / (2.0*a);
    double sol1 = (-b + sqrt(delta)) / (2.0*a);
    if (sol0 < 0.0 && sol1 < 0.0)
    return -1.0;
    if (sol0 < 0.0)
    return max(0.0, sol1);
    else if (sol1 < 0.0)
    return max(0.0, sol0);
    return max(0.0, min(sol0, sol1));
}

vec3 apply_wireframe(vec3 color, vec3 wireFrameColor, float wireframeSize, vec3 dist)
{
    if (wireframeSize > 0.0)
    {
        vec3 d2 = dist * dist;
        float nearest = min(min(d2.x, d2.y), d2.z);
        float f = exp2(-nearest / wireframeSize);
        color = mix(color, wireFrameColor, f);
    }
    return color;
}


#endif // LARGE_LEB_COMMON_FUNC_GLSL
