#ifndef ATMOSPHERE_COMON_FUNCTIONS_GLSL
#define ATMOSPHERE_COMON_FUNCTIONS_GLSL

#include "bruneton/definitions.glsl"
#include "common_defs.glsl"

float ClampCosine(float mu) {
    return clamp(mu, float(-1.0), float(1.0));
}

float ClampDistance(float d) {
    return max(d, 0.0 * m);
}

float ClampRadius(AtmosphereParameters atmosphere, float r) {
    return clamp(r, atmosphere.bottom_radius, atmosphere.top_radius);
}

float SafeSqrt(float a) {
    return sqrt(max(a, 0.0 * m2));
}

float RayleighPhaseFunction(float nu) {
    float k = 3.0 / (16.0 * PI * sr);
    return k * (1.0 + nu * nu);
}

float MiePhaseFunction(float g, float nu) {
    float k = 3.0 / (8.0 * PI * sr) * (1.0 - g * g) / (2.0 + g * g);
    return k * (1.0 + nu * nu) / pow(1.0 + g * g - 2.0 * g * nu, 1.5);
}

float IsotropicPhaseFunction() {
    return 1.0 / (4.0 * PI);
}


float GetLayerDensity(DensityProfileLayer layer, float altitude) {
    float density = layer.exp_term * exp(layer.exp_scale * altitude) + layer.linear_term * altitude + layer.constant_term;
    return clamp(density, float(0.0), float(1.0));
}

float GetProfileDensity(DensityProfile profile, float altitude) {
    return altitude < profile.layers[0].width ?
    GetLayerDensity(profile.layers[0], altitude) :
    GetLayerDensity(profile.layers[1], altitude);
}

float RaySphereIntersectNearest(vec3 rayOriign, vec3 rayDirection, vec3 sphereCenter, float sphereRadius) {
    vec3 r0 = rayOriign;
    vec3 rd = rayDirection;
    vec3 s0 = sphereCenter;
    float sR = sphereRadius;

    float a = dot(rd, rd);
    vec3 s0_r0 = r0 - s0;
    float b = 2.0 * dot(rd, s0_r0);
    float c = dot(s0_r0, s0_r0) - (sR * sR);
    float delta = b * b - 4.0*a*c;

    if (delta < 0.0 || a == 0.0){
        return -1.0;
    }
    float sol0 = (-b - SafeSqrt(delta)) / (2.0*a);
    float sol1 = (-b + SafeSqrt(delta)) / (2.0*a);
    if (sol0 < 0.0 && sol1 < 0.0){
        return -1.0;
    }
    if (sol0 < 0.0){
        return max(0.0, sol1);
    }
    else if (sol1 < 0.0){
        return max(0.0, sol0);
    }
    return max(0.0, min(sol0, sol1));
}

float DistanceToTopAtmosphereBoundary(AtmosphereParameters atmosphere, vec3 position, vec3 direction) {
    return RaySphereIntersectNearest(position, direction, vec3(0), atmosphere.top_radius);
}

float DistanceToBottomAtmosphereBoundary(AtmosphereParameters atmosphere, vec3 position, vec3 direction) {
    return RaySphereIntersectNearest(position, direction, vec3(0), atmosphere.bottom_radius);
}

float DistanceToAtmosphere(AtmosphereParameters atmosphere, vec3 position, vec3 direction) {
    float bottomAtmosphereDistance = DistanceToBottomAtmosphereBoundary(ATMOSPHERE, position, direction);
    float topAtmosphereDistance = DistanceToTopAtmosphereBoundary(ATMOSPHERE, position, direction);

    if ((bottomAtmosphereDistance == -1.0) && (topAtmosphereDistance == -1.0)){
        return -1.0;
    }
    else if ((bottomAtmosphereDistance == -1.0) && (topAtmosphereDistance > 0.0)){
        return topAtmosphereDistance;
    }
    else if ((bottomAtmosphereDistance > 0.0) && (topAtmosphereDistance == -1.0)){
        return bottomAtmosphereDistance;
    } else {
        return min(bottomAtmosphereDistance, topAtmosphereDistance);
    }
}

float DistanceToTopAtmosphereBoundary(AtmosphereParameters atmosphere, float r, float mu) {
    float discriminant = r * r * (mu * mu - 1.0) + atmosphere.top_radius * atmosphere.top_radius;
    return ClampDistance(-r * mu + SafeSqrt(discriminant));
}

float DistanceToBottomAtmosphereBoundary(AtmosphereParameters atmosphere, float r, float mu) {
    float discriminant = r * r * (mu * mu - 1.0) +  atmosphere.bottom_radius * atmosphere.bottom_radius;
    return ClampDistance(-r * mu - SafeSqrt(discriminant));
}

float GetTextureCoordFromUnitRange(float x, int texture_size) {
    return 0.5 / float(texture_size) + x * (1.0 - 1.0 / float(texture_size));
}

float GetUnitRangeFromTextureCoord(float u, int texture_size) {
    return (u - 0.5 / float(texture_size)) / (1.0 - 1.0 / float(texture_size));
}

vec2 GetUnitRangeFromTextureCoord(vec2 u, ivec2 texture_size) {
    return vec2(GetUnitRangeFromTextureCoord(u.x, texture_size.x), GetUnitRangeFromTextureCoord(u.y, texture_size.y));
}


float ComputeOpticalLengthToTopAtmosphereBoundary(AtmosphereParameters atmosphere, DensityProfile profile, float r, float mu) {
    const int SAMPLE_COUNT = 500;
    float dx = DistanceToTopAtmosphereBoundary(atmosphere, r, mu) / float(SAMPLE_COUNT);
    float result = 0.0 * m;
    for (int i = 0; i <= SAMPLE_COUNT; ++i) {
        float d_i = float(i) * dx;
        float r_i = sqrt(d_i * d_i + 2.0 * r * mu * d_i + r * r);
        float y_i = GetProfileDensity(profile, r_i - atmosphere.bottom_radius);
        float weight_i = i == 0 || i == SAMPLE_COUNT ? 0.5 : 1.0;
        result += y_i * weight_i * dx;
    }
    return result;
}

float ComputeOpticalLengthAt(AtmosphereParameters atmosphere, DensityProfile profile, vec3 position) {
    const float altitude = length(position) - atmosphere.bottom_radius;
    return GetProfileDensity(profile, altitude);
}

vec2 GetTransmittanceTextureUvFromRMu(AtmosphereParameters atmosphere, float r, float mu) {
    float H = sqrt(atmosphere.top_radius * atmosphere.top_radius -
    atmosphere.bottom_radius * atmosphere.bottom_radius);
    float rho =
    SafeSqrt(r * r - atmosphere.bottom_radius * atmosphere.bottom_radius);
    float d = DistanceToTopAtmosphereBoundary(atmosphere, r, mu);
    float d_min = atmosphere.top_radius - r;
    float d_max = rho + H;
    float x_mu = (d - d_min) / (d_max - d_min);
    float x_r = rho / H;

    return vec2(GetTextureCoordFromUnitRange(x_mu, TRANSMITTANCE_TEXTURE_SIZE.x),
                    GetTextureCoordFromUnitRange(x_r, TRANSMITTANCE_TEXTURE_SIZE.y));
}

void GetRMuFromTextureUv(AtmosphereParameters atmosphere, vec2 uv, ivec2 textureSize, out float r, out float mu) {
    float x_mu = GetUnitRangeFromTextureCoord(uv.x, textureSize.x);
    float x_r = GetUnitRangeFromTextureCoord(uv.y, textureSize.y);
    float H = sqrt(atmosphere.top_radius * atmosphere.top_radius -
    atmosphere.bottom_radius * atmosphere.bottom_radius);
    float rho = H * x_r;
    r = sqrt(rho * rho + atmosphere.bottom_radius * atmosphere.bottom_radius);
    float d_min = atmosphere.top_radius - r;
    float d_max = rho + H;
    float d = d_min + x_mu * (d_max - d_min);
    mu = d == 0.0 * m ? float(1.0) : (H * H - rho * rho - d * d) / (2.0 * r * d);
    mu = ClampCosine(mu);
}

vec3 GetTransmittanceToTopAtmosphereBoundary(AtmosphereParameters atmosphere, float r, float mu) {
    vec2 uv = GetTransmittanceTextureUvFromRMu(atmosphere, r, mu);
    return texture(transmittanceTexture, uv).rgb;
}


#endif // ATMOSPHERE_COMON_FUNCTIONS_GLSL