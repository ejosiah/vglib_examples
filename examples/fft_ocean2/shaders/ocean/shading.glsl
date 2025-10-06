#ifndef OCEAN_SHADING_GLSL
#define OCEAN_SHADING_GLSL

#include "uniforms.glsl"

struct Jacobian2D { float Dxx, Dxz, Dzx, Dzz; };

vec2 sampleNormal(vec2 p, int layer) {
    vec2 uv = fract(p/u.horizontalLength[layer]);
    vec3 loc = vec3(uv, layer);
    return texture(u_NormalSampler, loc).xy;
}

vec3 sampleNormal(vec2 p) {
    float Sx = 0, Sz = 0;

    const uint tileCount = 4;
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

Jacobian2D computeJacobian(vec2 p) {
    Jacobian2D  Jsum = Jacobian2D(0.0, 0.0, 0.0, 0.0);

    int count = int(u.tileCount); // e.g., 4
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

    return normalize(cross(Tz, Tx));  // choose cross order to match your winding
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

#endif // OCEAN_SHADING_GLSL