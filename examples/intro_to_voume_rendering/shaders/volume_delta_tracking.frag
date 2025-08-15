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

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------

void updateDepthBuffer(vec3 p);
float phase(float g, float cos_theta);
float sampleDensity(vec3 p);

// Ratio-tracked transmittance along a segment [0, tmax] on ray r
// inside the same spherical medium. Unbiased and step-size-free.
float ratioTransmittance(in Ray r, in float tmax)
{
    if (sigma_t_majorant <= 0.0) return 1.0;

    float T = 1.0;
    float t = 0.0;

    // guard against pathological loops
    const int MAX_ITERS = 1 << 14;
    int iter = 0;

    while (t < tmax && iter++ < MAX_ITERS) {
        float u  = max(1e-6, rand(rngState));
        float ds = -log(1.0 - u) / sigma_t_majorant;   // exponential free-flight
        t += ds;
        if (t >= tmax) break;

        vec3  x     = r.origin + t * r.direction;
        float rho   = clamp(sampleDensity(x), 0.0, density_majorant);
        float pcol  = clamp((rho * sigma_t) / sigma_t_majorant, 0.0, 0.999999);
        T *= (1.0 - pcol);
        if (T < 1e-6) { T = 0.0; break; }
    }

    return T;
}

void main()
{
    rngState = initRNG(gl_FragCoord.xy, resolution, frame);

    // Build the sphere in object/world space (same as your original)
    Sphere s;
    vec4 center = model * vec4(0, 0, 0, 1);
    s.center = center.xyz;
    s.radius = 0.8;

    // Intersect primary ray with the sphere
    vec2 cam_isect;
    Ray  r = ray;
    if (!sphere_ray_test(s, r, cam_isect)) {
        discard;
    }

    // Write depth at the *entry* point (same as before)
    vec3 hitPoint = r.origin + r.direction * cam_isect.x;
    updateDepthBuffer(hitPoint);

    // Precompute per-fragment view/light and phase
    vec3  V = normalize(-r.direction);
    vec3  L = normalize(lightDirection);
    float VDotL = dot(V, L);
    float phaseTerm = (g == 0.0)
    ? (1.0 / (4.0 * 3.14159265358979323846))
    : phase(g, VDotL);

    // -----------------------------------------------------------------
    // 1) Single-scatter via Delta (Woodcock) tracking on the view ray
    // -----------------------------------------------------------------
    vec3  result = vec3(0.0);

    // Delta tracking samples *true* first-collision distances when a
    // collision is accepted; no step size, no bands.
    float t  = cam_isect.x;
    float t1 = cam_isect.y;

    // Safety bailouts
    const int MAX_ITERS = 1 << 14;
    int iter = 0;

    // March using majorant σ̂_t = sigma_t_majorant
    while (t < t1 && iter++ < MAX_ITERS) {
        if (sigma_t_majorant <= 0.0) break;

        float u  = max(1e-6, rand(rngState));
        float ds = -log(1.0 - u) / sigma_t_majorant;   // exponential jump
        t += ds;

        if (t >= t1) break; // no real collision before exit

        // Candidate point
        vec3  x   = r.origin + t * r.direction;
        float rho = clamp(sampleDensity(x), 0.0, density_majorant);

        // Accept with prob p = σ_t(x) / σ̂_t
        float accept = (rho * sigma_t) / sigma_t_majorant;

        if (rand(rngState) < accept) {
            // ---- Real scattering at x ----
            // Next-event estimation: shadow transmittance via ratio tracking.
            Ray  rl = Ray(x, lightDirection);
            vec2 lseg;
            float T_light = 1.0;

            if (sphere_ray_test(s, rl, lseg) && lseg.x <= 1e-6) {
                T_light = ratioTransmittance(rl, lseg.y);
            }

            // With delta tracking, first-collision events are sampled
            // from the true distribution, so the weight here is just
            // the single-scatter albedo (σ_s/σ_t) times lighting.
            float albedo = sigma_t > 0.0 ? (sigma_s / sigma_t) : 0.0;

            result += lightColor * T_light * phaseTerm * albedo;

            // Single-scattering only: terminate after the first real event.
            break;
        }

        // Otherwise: null-collision, continue.
    }

    // -----------------------------------------------------------------
    // 2) View transmittance (alpha) via ratio tracking (unbiased)
    // -----------------------------------------------------------------
    // Estimate alpha as true transmittance through the medium segment.
    // This replaces the exponential-of-sum step scheme that banded.
    float T_view = ratioTransmittance(r, t1 - cam_isect.x);

    fragColor = vec4(result, T_view);
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
