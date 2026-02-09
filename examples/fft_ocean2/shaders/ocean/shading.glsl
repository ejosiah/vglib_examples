#ifndef OCEAN_SHADING_GLSL
#define OCEAN_SHADING_GLSL

#include "atmosphere/bruneton_api.glsl"
#include "uniforms.glsl"
#include "pbr/common.glsl"

float rcp(float x) { return 1.0/x; }

float Depth(float z, float near, float far) {
    return (near * far) / (far - z * (far - near));
}

struct Jacobian2D { float Dxx, Dxz, Dzx, Dzz, len; };

vec2 sampleNormal(vec2 p, int layer) {
    vec2 uv = fract(p/u.horizontalLength[layer]);
    vec3 loc = vec3(uv, layer);
    return texture(u_NormalSampler, loc).xy;
}

vec3 sampleNormal(vec2 p) {
    float Sx = 0, Sz = 0;

    const uint tileCount = u.tileCount;
    for(uint i = 0; i < tileCount; ++i){
        vec2 uv = fract(p/u.horizontalLength[i]);
        vec3 loc = vec3(uv, i);
        vec2 slope = texture(u_NormalSampler, loc).xy;

        Sx += slope.x;
        Sz += slope.y;
    }

    return normalize(vec3(-Sx, 1, -Sz));
}

Jacobian2D computeJacobian(vec2 wp, int layer)
{
    vec2 uv = fract(wp / u.horizontalLength[layer]);

    ivec2 sz = textureSize(u_DmapSampler, 0).xy;
    ivec2 p  = ivec2(floor(uv * vec2(sz)));

    ivec2 px = ivec2((p.x + 1) % sz.x, p.y);
    ivec2 nx = ivec2((p.x - 1 + sz.x) % sz.x, p.y);
    ivec2 py = ivec2(p.x, (p.y + 1) % sz.y);
    ivec2 ny = ivec2(p.x, (p.y - 1 + sz.y) % sz.y);

    vec4 R = texelFetch(u_DmapSampler, ivec3(px, layer), 0);
    vec4 L = texelFetch(u_DmapSampler, ivec3(nx, layer), 0);
    vec4 U = texelFetch(u_DmapSampler, ivec3(py, layer), 0);
    vec4 D = texelFetch(u_DmapSampler, ivec3(ny, layer), 0);

    float inv2dx = 0.5 * float(sz.x) / u.horizontalLength[layer];
    float inv2dz = 0.5 * float(sz.y) / u.horizontalLength[layer];

    Jacobian2D J;
    J.Dxx = (R.x - L.x) * inv2dx;   // ∂Dx/∂x
    J.Dxz = (U.x - D.x)  * inv2dz;   // ∂Dx/∂z
    J.Dzx = (R.z - L.z) * inv2dx;   // ∂Dz/∂x
    J.Dzz = (U.z - D.z)  * inv2dz;   // ∂Dz/∂z

    return J;
}

vec4 sampleJacobianNormal(vec2 p) {
    Jacobian2D J = Jacobian2D(0.0, 0.0, 0.0, 0.0, 0.0);
    float hx = 0, hz = 0;

    int count = int(u.tileCount);
    for (int layer = 0; layer < count; ++layer) {
        vec2 slope = textureLod(u_NormalSampler, vec3(p, float(layer)), 0.0).xy; // (∂h/∂x, ∂h/∂z)
        hx += slope.x;
        hz += slope.y;

        Jacobian2D Ji = computeJacobian(p, layer);
        J.Dxx += Ji.Dxx;
        J.Dxz += Ji.Dxz;
        J.Dzx += Ji.Dzx;
        J.Dzz += Ji.Dzz;
    }

    float lambda = u.choppiness;

    float Jxx = 1 + J.Dxx * lambda;
    float Jzz = 1 + J.Dzz * lambda;
    float Jzx = J.Dzx * lambda;
    float Jxz = J.Dxz * lambda;

    float Jlen = Jxx * Jzz - Jxz * Jzx;

    vec3 Tx = vec3(1.0 + J.Dxx, hx,        J.Dzx);
    vec3 Tz = vec3(       J.Dxz, hz, 1.0 + J.Dzz);

    return vec4(cross(Tz, Tx), Jlen);
}

Jacobian2D computeJacobian(vec2 p) {
    Jacobian2D  Jsum = Jacobian2D(0.0, 0.0, 0.0, 0.0, 0.0);

    int count = int(u.tileCount);
    for (int layer = 0; layer < count; ++layer) {

        Jacobian2D Ji = computeJacobian(p, layer);
        Jsum.Dxx += Ji.Dxx;
        Jsum.Dxz += Ji.Dxz;
        Jsum.Dzx += Ji.Dzx;
        Jsum.Dzz += Ji.Dzz;
    }
    return Jsum;
}

vec4 computeNormalAndJacobian(vec2 p) {
    float Sx = 0, Sz = 0;

    const uint tileCount = u.tileCount;
    for(uint i = 0; i < tileCount; ++i){
        vec2 uv = fract(p/u.horizontalLength[i]);
        vec3 loc = vec3(uv, i);
        vec2 slope = texture(u_NormalSampler, loc).xy;

        Sx += slope.x;
        Sz += slope.y;
    }
    vec3 n =  normalize(vec3(-Sx, 1, -Sz));

    Jacobian2D J = Jacobian2D(0.0, 0.0, 0.0, 0.0, 0.0);
    for (int layer = 0; layer < tileCount; ++layer) {

        Jacobian2D Ji = computeJacobian(p, layer);
        J.Dxx += Ji.Dxx;
        J.Dxz += Ji.Dxz;
        J.Dzx += Ji.Dzx;
        J.Dzz += Ji.Dzz;
    }

    float lambda = u.choppiness;
    float Jxx = 1 + J.Dxx * lambda;
    float Jzz = 1 + J.Dzz * lambda;
    float Jzx = J.Dzx * lambda;
    float Jxz = J.Dxz * lambda;

    float jD = Jxx * Jzz - Jxz * Jzx;

    return vec4(n, jD);
}

vec3 normalFromJacobian(vec2 p, int layer, Jacobian2D J)
{
    float L = u.horizontalLength[layer];
    vec2 uv = fract(p / L);

    vec2 slope = textureLod(u_NormalSampler, vec3(uv, float(layer)), 0.0).xy; // (∂h/∂x, ∂h/∂z)
    float hx = slope.x, hz = slope.y;

    vec3 Tx = vec3(1.0 + J.Dxx, hx,        J.Dzx);
    vec3 Tz = vec3(       J.Dxz, hz, 1.0 + J.Dzz);

    return normalize(cross(Tz, Tx));
}


vec3 mixWireFrame(vec3 srcColor, vec3 distance) {
    vec3 wireColor = vec3(1);

    const float wireScale = 0.8; // scale of the wire in pixel
    vec3 distanceSquared = distance * distance;
    float nearestDistance = min(min(distanceSquared.x, distanceSquared.y), distanceSquared.z);

    float t =  exp2(-nearestDistance / wireScale);

    return mix(srcColor, wireColor, t);
}

vec3 fresnel(vec3 R, vec3 N) {
    vec3 F0 = vec3(0.020018673);
    return F0 + (1.0 - F0) * pow(1.0 - dot(N, R), 5.0);
}


vec3 specular(vec3 N, vec3 V, vec3 H, vec3 L, float roughness) {
    const float ONE_OVER_4PI = 1.0/(4.0 * PI);

    const float rho = 0.3;
    const float ax = 0.25;
    const float ay = 0.1;

    vec3 X = normalize(cross(L, N));
    vec3 Y = normalize(cross(X, N));

    float HdotX = dot(H, X) / ax;
    float HdotY = dot(H, Y) / ay;
    float HdotN = dot(H, N);

    float mult = (ONE_OVER_4PI * rho / (ax * ay * sqrt(max(1e-5, dot(L, N) * dot(V, N)))));
    float spec = mult * exp(-((HdotX * HdotX) + (HdotY * HdotY)) / (HdotN * HdotN));

    return vec3(spec);
}

vec3 getAtmosphere(vec3 P, vec3 V, vec3 L) {
    vec3 transmittance;
    vec3 atm = GetSkyRadiance(P, V, 0, L, transmittance);
    if (dot(V, L) > u.sunSize.y) {
        atm += transmittance * GetSolarRadiance();
    }
    return atm;
}

float SchlickFresnel(vec3 normal, vec3 viewDir) {
    // 0.02f comes from the reflectivity bias of water kinda idk it's from a paper somewhere i'm not gonna link it tho lmaooo
    return 0.02f + (1 - 0.02f) * (pow(1 - max(0, dot(normal, viewDir)), 5.0f));
}

float SmithMaskingBeckmann(vec3 H, vec3 S, float roughness) {
    float hdots = max(0.001f, max(0, dot(H, S)));
    float a = hdots / (roughness * sqrt(1 - hdots * hdots));
    float a2 = a * a;

    return a < 1.6f ? (1.0f - 1.259f * a + 0.396f * a2) / (3.535f * a + 2.181 * a2) : 0.0f;
}

float Beckmann(float ndoth, float roughness) {
    float exp_arg = (ndoth * ndoth - 1) / (roughness * roughness * ndoth * ndoth);

    return exp(exp_arg) / (PI * roughness * roughness * ndoth * ndoth * ndoth * ndoth);
}

vec3 specular2(vec3 N, vec3 V, vec3 H, vec3 L, float roughness) {
    float NdotV = dot(N, V);
    float NdotL = dot(N, L);
    float NdotH = dot(N, H);

    float NDF = distributionGGX(N, H, roughness);
    float G   = geometrySmith(N, V, L, roughness);
    float  F   = SchlickFresnel(V, H);

    float  numerator = NDF * G * F;
    float  spec  = numerator / (4.0 * NdotV * NdotL + 1e-4);

//    float a = roughness;
//    float F = SchlickFresnel(N, V);
//    F = clamp(F, 0, 1);
//    float viewMask = SmithMaskingBeckmann(H, V, a);
//    float lightMask = SmithMaskingBeckmann(H, L, a);
//    float G = rcp(1 + viewMask + lightMask);
//    float NdotH = max(0.0001f, dot(N, H));
//    float spec = F * G * Beckmann(NdotH, a);
//    spec /= 4.0f * max(0.001f, max(0, dot(vec3(0, 1, 0), L)));
//    spec *= max(0, dot(N, L)); return vec3(spec);

    return vec3(spec);
}

vec3 scatteredLight(vec3 P, vec3 N, vec3 V, vec3 R, vec3 H, vec3 L, vec3 Lsun) {
    float _Roughness = 1;

    float waveHeight = max(0, P.y) * 0.1;
    float k1 = u.scatterConstants.x;
    float k2 = u.scatterConstants.y;
    float k3 = u.scatterConstants.z;
    float k4 = u.scatterConstants.w;
    vec3 Css = u.scatterColor.rgb;
    vec3 Cf = vec3(1);
    float Pf = 0;

    float a = _Roughness;
    float lamdaL = SmithMaskingBeckmann(H, L, a);

    vec3 Li = k1 * waveHeight * max(0, pow(dot(L, -V), 4)) * pow(0.5 - 0.5 * dot(L, N), 3) + k2 * pow(max(0, dot(V, N)), 2)
    * Css * Lsun * 1/(1 + lamdaL);

    Li += k3 * max(0, dot(L, N)) * Css * Lsun + k4 * Pf * Cf * Lsun;

    return Li;
}


vec3 shadeBasic(vec3 P, vec3 N, vec3 V, vec3 R, vec3 H, vec3 L) {
    vec3 Pa = P - u.earthCenter.xyz;
    vec3 skyIrradiance;
    vec3 sunIrradiance = GetSunAndSkyIrradiance(Pa, N, L, skyIrradiance);
    vec3 Lsun = skyIrradiance + sunIrradiance;

    vec3 env = getAtmosphere(Pa, R, L);
    vec3 F = fresnel(R, N);

    vec3 scatter = scatteredLight(P, N, V, R, H, L, Lsun);
    scatter = mix(scatter, env, F);

//    vec3 spec = specular(L, V, N);
    vec3 spec = specular2(N, V, H, L, 0.3);
    return scatter + spec * Lsun;
}

struct Shading{
    vec3 scatter;
    vec3 spec;
    vec3 env;
    vec3 sunLight;
    vec3 fresnel;
};

Shading shade(vec3 P, vec3 N, vec3 V, vec3 R, vec3 H, vec3 L ) {
    vec3 Pa = P - u.earthCenter.xyz;
    vec3 skyIrradiance;
    vec3 sunIrradiance = GetSunAndSkyIrradiance(Pa, N, L, skyIrradiance);
    vec3 Lsun = skyIrradiance + sunIrradiance;

    vec3 env = getAtmosphere(Pa, R, L);
    vec3 F = fresnel(N, L);

    vec3 scatter = scatteredLight(P, N, V, R, H, L, Lsun);

    //    vec3 spec = specular(L, V, N);
    vec3 spec = specular2(N, V, H, L, 0.3);

    return Shading(scatter, spec, env, Lsun, F);
}
#endif // OCEAN_SHADING_GLSL