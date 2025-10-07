#ifndef OCEAN_SHADING_GLSL
#define OCEAN_SHADING_GLSL

#include "atmosphere/bruneton_api.glsl"
#include "uniforms.glsl"

float linearizeDepth(float z, float near, float far) {
    return (near * far) / (far - z * (far - near));
}

struct Jacobian2D { float Dxx, Dxz, Dzx, Dzz; };

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

vec3 sampleJacobianNormal(vec2 p) {
    Jacobian2D J = Jacobian2D(0.0, 0.0, 0.0, 0.0);
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
    vec3 Tx = vec3(1.0 + J.Dxx, hx,        J.Dzx);
    vec3 Tz = vec3(       J.Dxz, hz, 1.0 + J.Dzz);

    return cross(Tz, Tx);
}

Jacobian2D computeJacobian(vec2 p) {
    Jacobian2D  Jsum = Jacobian2D(0.0, 0.0, 0.0, 0.0);

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

//vec3 mixTile(vec3 srcColor, vec2 p) {
//    if (!showTiles()) return srcColor;
//
//    // Inputs you can tune:
//    float tileLineWidthPx = 1.0;   // base 1px at reference size
//    float refTileLength   = u.horizontalLength[3]; // meters, choose your smallest L
//    float scaleExponent   = 1.0;   // 1 = linear growth, 0.5 = sqrt, etc.
//
//    // If you have per-tile domain lengths (meters)
//    vec4 tileLength = vec4(20, 15, 10, 5.00);
//
//    const vec3 tileColor[4] = vec3[4](
//    vec3(1.0, 0.0, 0.0),
//    vec3(0.0, 1.0, 0.0),
//    vec3(0.0, 0.0, 1.0),
//    vec3(1.0)
//    );
//
//    float bestMask  = 0.0;
//    vec3  bestColor = srcColor;
//
//    for (int i = 0; i < 4; ++i) {
//        // Tile UV in [0,1]
//        vec2 uv = fract(p/u.horizontalLength[i]);
//
//        // Chebyshev distance to outer edge (square outline)
//        float edge = max(abs(uv.x - 0.5), abs(uv.y - 0.5));
//
//        // Convert desired *pixel* thickness to *UV* using derivatives
//        vec2 duv = fwidth(uv);                 // UV change per pixel
//        float uvPerPx = max(max(duv.x, duv.y), 1e-6);
//
//        // Scale pixel thickness with tile size: thicker for larger L[i]
//        float Li     = tileLength[i];
//        float scale  = pow(Li / refTileLength, scaleExponent); // ≥ 1 for larger tiles
//        float w_uv   = (tileLineWidthPx * scale) * uvPerPx;    // UV thickness
//
//        // Anti-aliased edge mask (0 inside, 1 at the border and beyond)
//        float mask = smoothstep(0.5 - w_uv, 0.5, edge);
//
//        if (mask > bestMask) {
//            bestMask  = mask;
//            bestColor = tileColor[i];
//        }
//    }
//
//    return mix(srcColor, bestColor, bestMask);
//}


vec3 orennayar( vec3 L, vec3 V, vec3 N, float rho, float sigma ) {
    const float PI = 3.14159265358979323846;
    const float ONE_OVER_4PI = 1.0/(4.0 * PI);

    float VdotN = dot(V,N);
    float LdotN = dot(L,N);
    float theta_r = acos (VdotN);
    float sigma2 = pow(sigma*PI/180,2);

    float cos_phi_diff = dot( normalize(V-N*(VdotN)), normalize(L - N*(LdotN)) );
    float theta_i = acos (LdotN);
    float alpha = max (theta_i, theta_r);
    float beta = min (theta_i, theta_r);
    if (alpha > PI/2) return vec3(0);

    float C1 = 1 - 0.5 * sigma2 / (sigma2 + 0.33);
    float C2 = 0.45 * sigma2 / (sigma2 + 0.09);
    if (cos_phi_diff >= 0) C2 *= sin(alpha);
    else C2 *= (sin(alpha) - pow(2*beta/PI,3));
    float C3 = 0.125 * sigma2 / (sigma2+0.09) * pow ((4*alpha*beta)/(PI*PI),2);
    float L1 = rho/PI * (C1 + cos_phi_diff * C2 * tan(beta) + (1 - abs(cos_phi_diff)) * C3 * tan((alpha+beta)/2));
    float L2 = 0.17 * rho*rho / PI * sigma2/(sigma2+0.13) * (1 - cos_phi_diff*(4*beta*beta)/(PI*PI));
    return vec3(L1 + L2);
}

vec3 fresnel(vec3 R, vec3 N) {
    vec3 F0 = vec3(0.020018673);
    return F0 + (1.0 - F0) * pow(1.0 - dot(N, R), 5.0);
}

void swap(float a, float b) {
    float temp = a;
    b = a;
    a = temp;
}

vec3 fresnelDielectric(vec3 R, vec3 N) {
    float cosThetaI = dot(R, N);
    float etaI = 1;
    float etaT = 1.5;
    cosThetaI = clamp(cosThetaI, -1.0, 1.0);
    // Potentially swap indices of refraction
    bool entering = cosThetaI > 0.f;
    if (!entering) {
        swap(etaI, etaT);
        cosThetaI = abs(cosThetaI);
    }

    // Compute _cosThetaT_ using Snell's law
    float sinThetaI = sqrt(max(0, 1 - cosThetaI * cosThetaI));
    float sinThetaT = etaI / etaT * sinThetaI;

    // Handle total internal reflection
    if (sinThetaT >= 1) {
        return vec3(1);
    }
    float cosThetaT = sqrt(max(0, 1 - sinThetaT * sinThetaT));
    float Rparl = ((etaT * cosThetaI) - (etaI * cosThetaT)) /
    ((etaT * cosThetaI) + (etaI * cosThetaT));
    float Rperp = ((etaI * cosThetaI) - (etaT * cosThetaT)) /
    ((etaI * cosThetaI) + (etaT * cosThetaT));
    return vec3((Rparl * Rparl + Rperp * Rperp) / 2);
}

vec3 specular(vec3 L, vec3 V, vec3 N) {
    const float PI = 3.14159265358979323846;
    const float ONE_OVER_4PI = 1.0/(4.0 * PI);

    const float rho = 0.3;
    const float ax = 0.25;
    const float ay = 0.1;

    vec3 H = L + V;
    vec3 X = cross(L, N);
    vec3 Y = cross(X, N);

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

vec3 scatteredLight(vec3 P, vec3 N, vec3 V, vec3 R, vec3 L, vec3 scatterColor) {
    const vec3 Lsun	= vec3(1.0, 1.0, 0.47);
    float _Roughness = 1;

    float H = max(0, sampleDisplacement(P.xz).y) * 0.1;
    float k1 = 1;
    float k2 = 1;
    float k3 = 1;
    float k4 = 1;
    vec3 Css = scatterColor;
    vec3 Cf = vec3(1);
    float Pf = 0;

    float a = _Roughness;
    float lamdaL = SmithMaskingBeckmann(normalize(V + L), L, a);

    vec3 Li = k1 * H * max(0, pow(dot(L, -V), 4)) * pow(0.5 - 0.5 * dot(L, N), 3) + k2 * pow(max(0, dot(V, N)), 2)
    * Css * Lsun * 1/(1 + lamdaL);

    Li += k3 * max(0, dot(L, N)) * Css * Lsun + k4 * Pf * Cf * Lsun;

    return Li;
}


vec3 shadeBasic(vec3 P, vec3 N, vec3 V, vec3 R, vec3 L, vec3 scatterColor) {
    const float PI = 3.14159265358979323846;
    vec3 spec = specular(L, V, N);
    vec3 Pa = P - u.earthCenter.xyz;
    vec3 skyIrradiance;
    vec3 sunIrradiance = GetSunAndSkyIrradiance(Pa, N, L, skyIrradiance);

    vec3 env = getAtmosphere(Pa, R, L);
    vec3 F = fresnel(R, N);

    vec3 scatter = scatteredLight(P, N, V, R, L, scatterColor);
    scatter = mix(scatter, env, F);

    return scatter + spec * (skyIrradiance + sunIrradiance);
}

vec3 shade(vec3 P, vec3 N, vec3 V, vec3 R, vec3 L, vec3 scatterColor) {
    return vec3(1);
}

#endif // OCEAN_SHADING_GLSL