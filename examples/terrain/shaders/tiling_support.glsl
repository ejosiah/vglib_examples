#ifndef TILING_SUPPORT_GLSL
#define TILING_SUPPORT_GLSL

// ------------------------------------------------------------
// Common helpers
// ------------------------------------------------------------

float Saturate(float x) { return clamp(x, 0.0, 1.0); }
vec2  Saturate(vec2 x)  { return clamp(x, 0.0, 1.0); }
vec3  Saturate(vec3 x)  { return clamp(x, 0.0, 1.0); }

float Remap(float v, float inMin, float inMax, float outMin, float outMax)
{
    float t = (v - inMin) / max(inMax - inMin, 1e-6);
    return mix(outMin, outMax, t);
}

float Hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 rotate2D(vec2 p, float angleRadians)
{
    float s = sin(angleRadians);
    float c = cos(angleRadians);
    return mat2(c, -s, s, c) * p;
}

vec3 hueShift(vec3 color, float hue)
{
    const mat3 toYIQ = mat3(
    0.299,     0.587,     0.114,
    0.595716, -0.274453, -0.321263,
    0.211456, -0.522591,  0.311135
    );

    const mat3 toRGB = mat3(
    1.0,  0.9563,  0.6210,
    1.0, -0.2721, -0.6474,
    1.0, -1.1070,  1.7046
    );

    vec3 yiq = toYIQ * color;

    float a = atan(yiq.z, yiq.y) + hue * 6.28318530718;
    float chroma = length(yiq.yz);

    yiq.y = chroma * cos(a);
    yiq.z = chroma * sin(a);

    return Saturate(toRGB * yiq);
}

vec3 hsvAdjust(vec3 color, float hue, float saturation, float value)
{
    vec3 shifted = hueShift(color, hue);

    float luma = dot(shifted, vec3(0.299, 0.587, 0.114));
    shifted = mix(vec3(luma), shifted, saturation);

    shifted *= value;

    return Saturate(shifted);
}


// ------------------------------------------------------------
// Uber Mapping
// ------------------------------------------------------------

struct UberMappingResult
{
    vec3 vector;
    float gridView;
};

vec2 uberMapping(vec2 sourceUV, float scale, float aspectRatio, float translateX, float translateY,
                 float globalRotationDeg, float mosaicRotationDeg, float mosaicNoise, out float gridView) {
    vec2 uv = sourceUV;

    uv += vec2(translateX, translateY);

    uv.x *= max(aspectRatio, 1e-6);
    uv *= scale;

    uv -= 0.5;
    uv = rotate2D(uv, radians(globalRotationDeg));
    uv += 0.5;

    vec2 cell = floor(uv);
    vec2 local = fract(uv);

    float rnd = Hash12(cell);

    float mosaicAngle =
    radians(mosaicRotationDeg) *
    mix(-1.0, 1.0, rnd);

    mosaicAngle *= mosaicNoise;

    local -= 0.5;
    local = rotate2D(local, mosaicAngle);
    local += 0.5;

    vec2 mapped = cell + local;

    gridView =
    max(abs(local.x - 0.5), abs(local.y - 0.5)) * 2.0;

    return mapped;
}


// ------------------------------------------------------------
// PBR Mixer
// ------------------------------------------------------------

struct PBR {
    vec3 baseColor;
    float metalness;
    float roughness;
    vec3 normal;
    float displacement;
};

PBR pbrMixer(float fac, PBR a, PBR b){
    fac = Saturate(fac);

    PBR r;

    r.baseColor   = mix(a.baseColor,   b.baseColor,   fac);
    r.metalness   = mix(a.metalness,   b.metalness,   fac);
    r.roughness   = mix(a.roughness,   b.roughness,   fac);
    r.normal      = normalize(mix(a.normal, b.normal, fac));
    r.displacement = mix(a.displacement, b.displacement, fac);

    return r;
}


// ------------------------------------------------------------
// Color Variation
// ------------------------------------------------------------

vec3 colorVariation(vec3 imageColor, vec3 variationCoord, float scale, float hueAmount, float saturationAmount,
                    float valueAmount, float darkSpotAmount, float darkSpotScale) {
    vec2 p = variationCoord.xy * scale;

    float seed1 = Hash12(floor(p));
    float seed2 = Hash12(floor(p + 19.17));
    float seed3 = Hash12(floor(p + 73.31));

    float hue = (seed1 - 0.5) * hueAmount;
    float sat = 1.0 + (seed2 - 0.5) * saturationAmount;
    float val = 1.0 + (seed3 - 0.5) * valueAmount;

    vec3 color = hsvAdjust(imageColor, hue, sat, val);

    vec2 spotP = variationCoord.xy * darkSpotScale;

    float spot = Hash12(floor(spotP));
    spot = smoothstep(1.0 - darkSpotAmount, 1.0, spot);

    color = mix(color, color * 0.45, spot * darkSpotAmount);

    return Saturate(color);
}

#endif // TILING_SUPPORT_GLSL