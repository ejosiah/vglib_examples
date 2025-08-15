#version 460 core
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_scalar_block_layout : enable

#include "random.glsl"
#include "sampling.glsl"
#include "raytracing_implicits/implicits.glsl"
#include "perlin_noise.glsl"

layout(set = 1, binding = 0, scalar) uniform Globals {
    mat4  projection;
    mat4  view;
    vec2  resolution;
    float near;
    float far;
    uint  frame;
    uint  color_tex_id;
    uint  depth_tex_id;
};

layout(set = 0, binding = 10) uniform sampler2D global_textures[];

layout(location = 0) in Ray  ray;
layout(location = 2) in mat4 model;

layout(location = 0) out vec4 fragColor;

// ---------------------------------------------------------------------
// Medium / lighting
// ---------------------------------------------------------------------
const vec3  lightColor       = vec3(20.0);
const vec3  lightDirection   = vec3(-0.315798, 0.719361, 0.618702);
const float g                = 0.0;         // Henyey-Greenstein anisotropy
const float sigma_a          = 10.0;
const float sigma_s          = 5.0;
const float sigma_t          = sigma_a + sigma_s;
const float epsilon          = 1e-3;
const float rr               = 2.0;

// Your density is abs(perlin)*0.2 (falloff = 0.8), so ρ(x) ∈ [0, 0.2].
// Add a small safety factor for the majorant:
const float density_majorant = 0.2 * 1.05;

// Majorant extinction used by delta/ratio tracking:
const float sigma_t_majorant = sigma_t * density_majorant;

RngStateType rngState;

// --- helper: unbiased shadow transmittance (ratio tracking) ---
float ratioTransmittance(in Ray r, float tmax, float sigma_t_majorant);
void updateDepthBuffer(vec3 p);
float phase(float g, float cos_theta);
float sampleDensity(vec3 p);

void main(){
    rngState = initRNG(gl_FragCoord.xy, resolution, frame);

    // Build sphere (same as yours)
    Sphere s;
    vec4 center = model * vec4(0, 0, 0, 1);
    s.center = center.xyz;
    s.radius = 0.8;

    // Primary ray/sphere hit
    vec2 cam_isect;
    Ray r = ray;
    if(!sphere_ray_test(s, r, cam_isect)) {
        discard;
    }

    // Depth at entry
    vec3 hitPoint = r.origin + r.direction * cam_isect.x;
    updateDepthBuffer(hitPoint);

    // Per-fragment precomputes
    vec3 V = normalize(-r.direction);
    vec3 L = normalize(lightDirection);
    float VDotL = dot(V, L);
    float phaseTerm = (g == 0.0) ? (1.0 / (4.0 * 3.14159265358979323846)) : phase(g, VDotL);
    float sigma_s_phase = sigma_s * phaseTerm;

    // ------- HYBRID: fixed-step view march (no ceil), ratio-tracked shadow -------
    // Choose a fixed step size (tune as you like); remove ceil/stride coupling.
    float step_size = 0.2;

    float t0 = cam_isect.x;
    float t1 = cam_isect.y;

    // One jitter offset per view ray (blue-noise is even better if you have it)
    float u_view = rand(rngState);
    float t = t0 + u_view * step_size;

    // Accumulate optical depth for stable transparency
    float tau_view = 0.0;
    float transparency = 1.0;

    vec3 result = vec3(0.0);

    // Precompute a safe majorant for the shadow ratio tracking.
    // Your density is abs(perlin)*0.2 (falloff=0.8), so <= 0.2; add a small slack:
    const float density_majorant = 0.2 * 1.05;
    const float sigma_t_majorant = sigma_t * density_majorant;

    for (; t < t1; t += step_size) {
        vec3 p = r.origin + t * r.direction;

        float density = sampleDensity(p);          // rho(x) in [0,1] scaled by your function
        float dTau    = density * sigma_t * step_size;
        tau_view     += dTau;
        transparency  = exp(-tau_view);

        if (density > 0.0) {
            // Shadow transmittance via ratio tracking (no step bands on the shadow)
            Ray rl = Ray(p, lightDirection);
            vec2 lseg;
            float T_light = 1.0;
            if (sphere_ray_test(s, rl, lseg) && lseg.x <= 1e-6) {
                T_light = ratioTransmittance(rl, lseg.y, sigma_t_majorant);
            }

            // Single-scatter estimator (view march × shadow ratio-tracking)
            result += lightColor * T_light * sigma_s_phase * transparency * density * step_size;
        }

        // RR on very low T to keep work bounded
        if (transparency < epsilon) {
            if (rand(rngState) > 1.0 / rr) break;
            // Boost weight by rr: reduce tau_view by log(rr)
            tau_view = max(0.0, tau_view - log(rr));
            transparency = exp(-tau_view);
        }
    }

    fragColor = vec4(result, transparency);
}

float ratioTransmittance(in Ray r, float tmax, float sigma_t_majorant)
{
    if (sigma_t_majorant <= 0.0) return 1.0;

    float T = 1.0;
    float t = 0.0;
    const int MAX_ITERS = 1 << 14;
    int it = 0;

    while (t < tmax && it++ < MAX_ITERS) {
        float u  = max(1e-6, rand(rngState));
        float ds = -log(1.0 - u) / sigma_t_majorant; // exponential free-flight
        t += ds;
        if (t >= tmax) break;

        vec3  x   = r.origin + t * r.direction;
        float rho = clamp(sampleDensity(x), 0.0, 1.0);
        float p   = clamp((rho * sigma_t) / sigma_t_majorant, 0.0, 0.999999);
        T *= (1.0 - p);
        if (T < 1e-6) { T = 0.0; break; }
    }
    return T;
}

void updateDepthBuffer(vec3 p) {
    vec4 clipPos = projection * view * vec4(p, 1);
    clipPos.xyz /= clipPos.w;
    gl_FragDepth = clipPos.z;
}

float phase(float g, float cos_theta) {
    float denom = 1 + g * g - 2 * g * cos_theta;
    return 1 / (4 * 3.14159265358979323846) * (1 - g * g) / (denom * sqrt(denom));
}

float sampleDensity(vec3 p) {
    const float falloff = 0.2;
    return abs(perlin(p * 8)) * (1 - falloff);
}
