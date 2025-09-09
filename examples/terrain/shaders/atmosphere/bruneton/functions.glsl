#ifndef ATMOSPHERE_BRUNETON_FUNCTIONS_GLSL
#define ATMOSPHERE_BRUNETON_FUNCTIONS_GLSL

#include "definitions.glsl"

#ifndef TRANSMITTANCE_TEXTURE_SIZE
#define TRANSMITTANCE_TEXTURE_SIZE ivec2(64, 16)
#endif // TRANSMITTANCE_TEXTURE_SIZE

#define TRANSMITTANCE_TEXTURE_WIDTH  56
#define TRANSMITTANCE_TEXTURE_HEIGHT 64
#define SCATTERING_TEXTURE_R_SIZE 32
#define SCATTERING_TEXTURE_MU_SIZE  28
#define SCATTERING_TEXTURE_MU_S_SIZE 32
#define SCATTERING_TEXTURE_NU_SIZE 8
#define IRRADIANCE_TEXTURE_WIDTH 64
#define IRRADIANCE_TEXTURE_HEIGHT 16

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

bool RayIntersectsGround(AtmosphereParameters atmosphere, float r, float mu) {
    return mu < 0.0 && r * r * (mu * mu - 1.0) + atmosphere.bottom_radius * atmosphere.bottom_radius >= 0.0 * m2;
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
    const int SAMPLE_COUNT = 40;
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
    return texture(TRANSMITTANCE_TEXTURE, uv).rgb;
}

vec3 GetTransmittanceToTopAtmosphereBoundary(AtmosphereParameters atmosphere, sampler2D transmittance_texture, float r, float mu) {
    vec2 uv = GetTransmittanceTextureUvFromRMu(atmosphere, r, mu);
    return vec3(texture(transmittance_texture, uv));
}

vec3 GetTransmittance(AtmosphereParameters atmosphere, sampler2D transmittance_texture, float r, float mu, float d,
                    bool ray_r_mu_intersects_ground) {
    float r_d = ClampRadius(atmosphere, sqrt(d * d + 2.0 * r * mu * d + r * r));
    float mu_d = ClampCosine((r * mu + d) / r_d);
    if (ray_r_mu_intersects_ground) {
        return min(
        GetTransmittanceToTopAtmosphereBoundary(
        atmosphere, transmittance_texture, r_d, -mu_d) /
        GetTransmittanceToTopAtmosphereBoundary(
        atmosphere, transmittance_texture, r, -mu),
        vec3(1.0));
    } else {
        return min(
        GetTransmittanceToTopAtmosphereBoundary(
        atmosphere, transmittance_texture, r, mu) /
        GetTransmittanceToTopAtmosphereBoundary(
        atmosphere, transmittance_texture, r_d, mu_d),
        vec3(1.0));
    }
}

vec4 GetScatteringTextureUvwzFromRMuMuSNu(AtmosphereParameters atmosphere,
float r, float mu, float mu_s, float nu,
bool ray_r_mu_intersects_ground) {
    float H = sqrt(atmosphere.top_radius * atmosphere.top_radius -
    atmosphere.bottom_radius * atmosphere.bottom_radius);
    float rho =
    SafeSqrt(r * r - atmosphere.bottom_radius * atmosphere.bottom_radius);
    float u_r = GetTextureCoordFromUnitRange(rho / H, SCATTERING_TEXTURE_R_SIZE);
    float r_mu = r * mu;
    float discriminant =
    r_mu * r_mu - r * r + atmosphere.bottom_radius * atmosphere.bottom_radius;
    float u_mu;
    if (ray_r_mu_intersects_ground) {
        float d = -r_mu - SafeSqrt(discriminant);
        float d_min = r - atmosphere.bottom_radius;
        float d_max = rho;
        u_mu = 0.5 - 0.5 * GetTextureCoordFromUnitRange(d_max == d_min ? 0.0 :
        (d - d_min) / (d_max - d_min), SCATTERING_TEXTURE_MU_SIZE / 2);
    } else {
        float d = -r_mu + SafeSqrt(discriminant + H * H);
        float d_min = atmosphere.top_radius - r;
        float d_max = rho + H;
        u_mu = 0.5 + 0.5 * GetTextureCoordFromUnitRange(
        (d - d_min) / (d_max - d_min), SCATTERING_TEXTURE_MU_SIZE / 2);
    }
    float d = DistanceToTopAtmosphereBoundary(
    atmosphere, atmosphere.bottom_radius, mu_s);
    float d_min = atmosphere.top_radius - atmosphere.bottom_radius;
    float d_max = H;
    float a = (d - d_min) / (d_max - d_min);
    float D = DistanceToTopAtmosphereBoundary(
    atmosphere, atmosphere.bottom_radius, atmosphere.mu_s_min);
    float A = (D - d_min) / (d_max - d_min);
    float u_mu_s = GetTextureCoordFromUnitRange(
    max(1.0 - a / A, 0.0) / (1.0 + a), SCATTERING_TEXTURE_MU_S_SIZE);
    float u_nu = (nu + 1.0) / 2.0;
    return vec4(u_nu, u_mu_s, u_mu, u_r);
}

vec3 GetCombinedScattering(
AtmosphereParameters atmosphere,
sampler3D scattering_texture,
sampler3D single_mie_scattering_texture,
float r, float mu, float mu_s, float nu,
bool ray_r_mu_intersects_ground,
out vec3 single_mie_scattering) {
    vec4 uvwz = GetScatteringTextureUvwzFromRMuMuSNu(
    atmosphere, r, mu, mu_s, nu, ray_r_mu_intersects_ground);
    float tex_coord_x = uvwz.x * float(SCATTERING_TEXTURE_NU_SIZE - 1);
    float tex_x = floor(tex_coord_x);
    float lerp = tex_coord_x - tex_x;
    vec3 uvw0 = vec3((tex_x + uvwz.y) / float(SCATTERING_TEXTURE_NU_SIZE),
    uvwz.z, uvwz.w);
    vec3 uvw1 = vec3((tex_x + 1.0 + uvwz.y) / float(SCATTERING_TEXTURE_NU_SIZE),
    uvwz.z, uvwz.w);
    #ifdef COMBINED_SCATTERING_TEXTURES
    vec4 combined_scattering =
    texture(scattering_texture, uvw0) * (1.0 - lerp) +
    texture(scattering_texture, uvw1) * lerp;
    vec3 scattering = vec3(combined_scattering);
    single_mie_scattering =
    GetExtrapolatedSingleMieScattering(atmosphere, combined_scattering);
    #else
    vec3 scattering = vec3(
    texture(scattering_texture, uvw0) * (1.0 - lerp) +
    texture(scattering_texture, uvw1) * lerp);
    single_mie_scattering = vec3(
    texture(single_mie_scattering_texture, uvw0) * (1.0 - lerp) +
    texture(single_mie_scattering_texture, uvw1) * lerp);
    #endif
    return scattering;
}


vec3 GetSkyRadiance(
    AtmosphereParameters atmosphere,
    sampler2D transmittance_texture,
    sampler3D scattering_texture,
    sampler3D single_mie_scattering_texture,
    vec3 camera, vec3 view_ray, float shadow_length,
    vec3 sun_direction, out vec3 transmittance
) {

    float r = length(camera);
    float rmu = dot(camera, view_ray);
    float distance_to_top_atmosphere_boundary = -rmu -
    sqrt(rmu * rmu - r * r +
    atmosphere.top_radius * atmosphere.top_radius);
    if (distance_to_top_atmosphere_boundary > 0.0 * m) {
        camera = camera + view_ray * distance_to_top_atmosphere_boundary;
        r = atmosphere.top_radius;
        rmu += distance_to_top_atmosphere_boundary;
    } else if (r > atmosphere.top_radius) {
        transmittance = vec3(1.0);
        return vec3(0.0 * watt_per_square_meter_per_sr_per_nm);
    }
    float mu = rmu / r;
    float mu_s = dot(camera, sun_direction) / r;
    float nu = dot(view_ray, sun_direction);
    bool ray_r_mu_intersects_ground = RayIntersectsGround(atmosphere, r, mu);
    transmittance = ray_r_mu_intersects_ground ? vec3(0.0) :
    GetTransmittanceToTopAtmosphereBoundary(
    atmosphere, transmittance_texture, r, mu);
    vec3 single_mie_scattering;
    vec3 scattering;
    if (shadow_length == 0.0 * m) {
        scattering = GetCombinedScattering(
        atmosphere, scattering_texture, single_mie_scattering_texture,
        r, mu, mu_s, nu, ray_r_mu_intersects_ground,
        single_mie_scattering);
    } else {
        float d = shadow_length;
        float r_p =
        ClampRadius(atmosphere, sqrt(d * d + 2.0 * r * mu * d + r * r));
        float mu_p = (r * mu + d) / r_p;
        float mu_s_p = (r * mu_s + d * nu) / r_p;
        scattering = GetCombinedScattering(
        atmosphere, scattering_texture, single_mie_scattering_texture,
        r_p, mu_p, mu_s_p, nu, ray_r_mu_intersects_ground,
        single_mie_scattering);
        vec3 shadow_transmittance =
        GetTransmittance(atmosphere, transmittance_texture,
        r, mu, shadow_length, ray_r_mu_intersects_ground);
        scattering = scattering * shadow_transmittance;
        single_mie_scattering = single_mie_scattering * shadow_transmittance;
    }
    return scattering * RayleighPhaseFunction(nu) + single_mie_scattering *
    MiePhaseFunction(atmosphere.mie_phase_function_g, nu);
}

vec3 GetSkyRadianceToPoint(
AtmosphereParameters atmosphere,
sampler2D transmittance_texture,
sampler3D scattering_texture,
sampler3D single_mie_scattering_texture,
vec3 camera, vec3 point, float shadow_length,
vec3 sun_direction, out vec3 transmittance
) {

    vec3 view_ray = normalize(point - camera);
    float r = length(camera);
    float rmu = dot(camera, view_ray);
    float distance_to_top_atmosphere_boundary = -rmu -
    sqrt(rmu * rmu - r * r +
    atmosphere.top_radius * atmosphere.top_radius);

    //    debugPrintfEXT("r: %f, dist_top: %f\n", r, distance_to_top_atmosphere_boundary);

    if (distance_to_top_atmosphere_boundary > 0.0 * m) {
        camera = camera + view_ray * distance_to_top_atmosphere_boundary;
        r = atmosphere.top_radius;
        rmu += distance_to_top_atmosphere_boundary;
    }
    float mu = rmu / r;
    float mu_s = dot(camera, sun_direction) / r;
    float nu = dot(view_ray, sun_direction);
    float d = length(point - camera);
    bool ray_r_mu_intersects_ground = RayIntersectsGround(atmosphere, r, mu);
    transmittance = GetTransmittance(atmosphere, transmittance_texture,
    r, mu, d, ray_r_mu_intersects_ground);
    vec3 single_mie_scattering;
    vec3 scattering = GetCombinedScattering(
    atmosphere, scattering_texture, single_mie_scattering_texture,
    r, mu, mu_s, nu, ray_r_mu_intersects_ground,
    single_mie_scattering);
    d = max(d - shadow_length, 0.0 * m);
    float r_p = ClampRadius(atmosphere, sqrt(d * d + 2.0 * r * mu * d + r * r));
    float mu_p = (r * mu + d) / r_p;
    float mu_s_p = (r * mu_s + d * nu) / r_p;
    vec3 single_mie_scattering_p;
    vec3 scattering_p = GetCombinedScattering(
    atmosphere, scattering_texture, single_mie_scattering_texture,
    r_p, mu_p, mu_s_p, nu, ray_r_mu_intersects_ground,
    single_mie_scattering_p);
    vec3 shadow_transmittance = transmittance;
    if (shadow_length > 0.0 * m) {
        shadow_transmittance = GetTransmittance(atmosphere, transmittance_texture,
        r, mu, d, ray_r_mu_intersects_ground);
    }
    scattering = scattering - shadow_transmittance * scattering_p;
    single_mie_scattering =
    single_mie_scattering - shadow_transmittance * single_mie_scattering_p;
    #ifdef COMBINED_SCATTERING_TEXTURES
    single_mie_scattering = GetExtrapolatedSingleMieScattering(
    atmosphere, vec4(scattering, single_mie_scattering.r));
    #endif
    single_mie_scattering = single_mie_scattering *
    smoothstep(float(0.0), float(0.01), mu_s);
    return scattering * RayleighPhaseFunction(nu) + single_mie_scattering *
    MiePhaseFunction(atmosphere.mie_phase_function_g, nu);
}

vec3 GetTransmittanceToSun(AtmosphereParameters atmosphere, sampler2D transmittance_texture, float r, float mu_s) {
    float sin_theta_h = atmosphere.bottom_radius / r;
    float cos_theta_h = -sqrt(max(1.0 - sin_theta_h * sin_theta_h, 0.0));
    return GetTransmittanceToTopAtmosphereBoundary(
        atmosphere, transmittance_texture, r, mu_s) *
        smoothstep(-sin_theta_h * atmosphere.sun_angular_radius / rad,
        sin_theta_h * atmosphere.sun_angular_radius / rad,
        mu_s - cos_theta_h);
}

vec2 GetIrradianceTextureUvFromRMuS(AtmosphereParameters atmosphere, float r, float mu_s) {
    float x_r = (r - atmosphere.bottom_radius) /
    (atmosphere.top_radius - atmosphere.bottom_radius);
    float x_mu_s = mu_s * 0.5 + 0.5;
    return vec2(GetTextureCoordFromUnitRange(x_mu_s, IRRADIANCE_TEXTURE_WIDTH),
    GetTextureCoordFromUnitRange(x_r, IRRADIANCE_TEXTURE_HEIGHT));
}

vec3 GetIrradiance(AtmosphereParameters atmosphere, sampler2D irradiance_texture, float r, float mu_s) {
    vec2 uv = GetIrradianceTextureUvFromRMuS(atmosphere, r, mu_s);
    return vec3(texture(irradiance_texture, uv));
}


vec3 GetSunAndSkyIrradiance(
AtmosphereParameters atmosphere,
sampler2D transmittance_texture,
sampler2D irradiance_texture,
vec3 point, vec3 normal, vec3 sun_direction,
out vec3 sky_irradiance) {
    float r = length(point);
    float mu_s = dot(point, sun_direction) / r;
    sky_irradiance = GetIrradiance(atmosphere, irradiance_texture, r, mu_s) *
    (1.0 + dot(normal, point) / r) * 0.5;
    return atmosphere.solar_irradiance *
    GetTransmittanceToSun(
    atmosphere, transmittance_texture, r, mu_s) *
    max(dot(normal, sun_direction), 0.0);
}

#endif // ATMOSPHERE_BRUNETON_FUNCTIONS_GLSL
