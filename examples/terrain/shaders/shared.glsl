#ifndef TERRAIN_SHARED_GLSL
#define TERRAIN_SHARED_GLSL

#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_debug_printf : enable

#define CBT_HEAP_BUFFER_BINDING 0

#include "leb.glsl"
#include "frustum_culling.glsl"
#include "pbr/common.glsl"
#include "hash.glsl"

const uint PROJECTION_RECTILINEAR = 0u;
const uint PROJECTION_ORTHOGRAPHIC = 1u;
const uint PROJECTION_FISHEYE = 2u;

#ifndef DISPLACE_CONST_ID
#define DISPLACE_CONST_ID 2
#endif // DISPLACE_CONST_ID

#ifndef PROJECTION_METHOD_CONST_ID
#define PROJECTION_METHOD_CONST_ID 3
#endif // PROJECTION_METHOD_CONST_ID

#ifndef TRIANGLE_CULL_CONST_ID
#define TRIANGLE_CULL_CONST_ID 4
#endif // TRIANGLE_CULL_CONST_ID

layout(constant_id = DISPLACE_CONST_ID) const uint flag_displace = 0;
layout(constant_id = PROJECTION_METHOD_CONST_ID) const uint projection_method = PROJECTION_RECTILINEAR;
layout(constant_id = TRIANGLE_CULL_CONST_ID) const uint cull_triangle = 0;

const bool should_displace = flag_displace == 1;
const bool should_cull_triangle = cull_triangle == 1;

layout(set = 2, binding = 0, scalar) buffer Constants {
    mat4 modelMatrix;
    mat4 modelViewMatrix;
    mat4 viewMatrix;
    mat4 cameraMatrix;
    mat4 viewProjectionMatrix;
    mat4 modelViewProjectionMatrix;
    vec4 frustumPlanes[6];
    ivec4 mouse;
    vec3 lightDirection;
    vec3 whitePoint;
    vec2 resolution;
    vec2 sunSize;
    vec2 tileSize;
    float exposure;
    float lodFactor;
    float minLodVariance;
    float dmapFactor;
    float blendMin;
    float blendMax;
    uint minArea;
    uint showTiles;
    uint tileColor;
    uint wireframeOn;
    uint useTriplanerMapping;
    uint damp_tex_index;
    uint dmap_normal_tex_index;
    uint shadow_tex_index;
    uint dirtAlbedoMapIndex;
    uint dirtAoMapIndex;
    uint dirtRoughnessMapIndex;
    uint dirtNormalMapIndex;
    uint grassAlbedoMapIndex;
    uint grassAoMapIndex;
    uint grassRoughnessMapIndex;
    uint grassNormalMapIndex;
} globals;

bool wireframeEnabled() {
    return globals.wireframeOn == 1;
}

bool showTiles() {
    return globals.showTiles == 1;
}

bool useTriplanerMapping() {
    return globals.useTriplanerMapping == 1;
}

layout(set = 1, binding = 10) uniform sampler2D global_textures[];
layout(set = 1, binding = 10) uniform sampler3D global_textures_3d[];

// TODO add damp_tex_index to uniforms
#define u_DmapSampler global_textures[nonuniformEXT(globals.damp_tex_index)]
#define u_NormalSampler global_textures[nonuniformEXT(globals.dmap_normal_tex_index)]
#define u_DmapShadowSampler global_textures[nonuniformEXT(globals.shadow_tex_index)]

#define dirtAlbedoMap global_textures[nonuniformEXT(globals.dirtAlbedoMapIndex)]
#define dirtAoMap global_textures[nonuniformEXT(globals.dirtAoMapIndex)]
#define dirtRoughnessMap global_textures[nonuniformEXT(globals.dirtRoughnessMapIndex)]
#define dirtNormalMap global_textures[nonuniformEXT(globals.dirtNormalMapIndex)]

#define grassAlbedoMap global_textures[nonuniformEXT(globals.grassAlbedoMapIndex)]
#define grassAoMap global_textures[nonuniformEXT(globals.grassAoMapIndex)]
#define grassRoughnessMap global_textures[nonuniformEXT(globals.grassRoughnessMapIndex)]
#define grassNormalMap global_textures[nonuniformEXT(globals.grassNormalMapIndex)]

struct Material {
    vec3 albedo;
    vec3 normal;
    float metalness;
    float roughness;
    float ao;
    float blend;
};

vec3 shadeFragment(Material material, vec3 N, vec3 V, vec3 L, float visiblity, vec3 sunTransmittance, vec3 ambientIrradiance)
{
    vec3  albedo    = material.albedo;
    float metalness = material.metalness;
    float roughness = material.roughness;
    float ao        = material.ao;

    float shadowVis = clamp(visiblity, 0.0, 1.0);
    vec3  sunT      = clamp(sunTransmittance, 0.0, 1.0);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    vec3  H     = normalize(V + L);

    // Sun color/intensity at top of atmosphere (your scale)
    vec3 sunIrradiance = vec3(10.0);

    // Direct sun radiance after shadowing + atmospheric transmission
    vec3 radiance = sunIrradiance * shadowVis * sunT;

    // Cook–Torrance terms
    float NDF = distributionGGX(N, H, roughness);
    float G   = geometrySmith(N, V, L, roughness);
    vec3  F0  = mix(vec3(0.04), albedo, metalness);
    vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3  numerator = NDF * G * F;
    vec3  specular  = numerator / (4.0 * NdotV * NdotL + 1e-4);

    vec3  kS = F;
    vec3  kD = (vec3(1.0) - kS) * (1.0 - metalness);
    vec3 diffuse = kD * albedo / PI;

    specular = vec3(0);

    // Direct lighting (sun); AO is NOT applied to direct by default
    vec3 Lo = (diffuse + specular) * radiance * NdotL;

    // Global ambient / skylight (diffuse only), AO applied here
    vec3 ambient = diffuse * ambientIrradiance * ao;

    return Lo + ambient;
//    return diffuse * radiance * NdotL + ambient;
}


/*******************************************************************************
 * FrustumCullingTest -- Checks if the triangle lies inside the view frutsum
 *
 * This function depends on FrustumCulling.glsl
 *
 */
bool FrustumCullingTest(in const vec4[3] patchVertices)
{
    vec3 bmin = min(min(patchVertices[0].xyz, patchVertices[1].xyz), patchVertices[2].xyz);
    vec3 bmax = max(max(patchVertices[0].xyz, patchVertices[1].xyz), patchVertices[2].xyz);

    return FrustumCullingTest(globals.frustumPlanes, bmin, bmax);
}


/*******************************************************************************
 * DecodeTriangleVertices -- Decodes the triangle vertices in local space
 *
 */
vec4[3] DecodeTriangleVertices(in const cbt_Node node)
{
    vec3 xPos = vec3(0, 0, 1), yPos = vec3(1, 0, 0);
    mat2x3 pos = leb_DecodeNodeAttributeArray_Square(node, mat2x3(xPos, yPos));
    vec4 p1 = vec4(pos[0][0], pos[1][0], 0.0, 1.0);
    vec4 p2 = vec4(pos[0][1], pos[1][1], 0.0, 1.0);
    vec4 p3 = vec4(pos[0][2], pos[1][2], 0.0, 1.0);

    if(should_displace) {
        p1.z = globals.dmapFactor * texture(u_DmapSampler, p1.xy).r;
        p2.z = globals.dmapFactor * texture(u_DmapSampler, p2.xy).r;
        p3.z = globals.dmapFactor * texture(u_DmapSampler, p3.xy).r;
    }

    return vec4[3](p1, p2, p3);
}

/*******************************************************************************
 * TriangleLevelOfDetail -- Computes the LoD assocaited to a triangle
 *
 * This function is used to garantee a user-specific pixel edge length in
 * screen space. The reference edge length is that of the longest edge of the
 * input triangle.In practice, we compute the LoD as:
 *      LoD = 2 * log2(EdgePixelLength / TargetPixelLength)
 * where the factor 2 is because the number of segments doubles every 2
 * subdivision level.
 */
float TriangleLevelOfDetail_Perspective(in const vec4[3] patchVertices)
{

    vec3 p0 = patchVertices[0].xyz;
    vec3 p2 = patchVertices[2].xyz;
    vec3 v0 = (globals.modelViewMatrix * patchVertices[0]).xyz;
    vec3 v2 = (globals.modelViewMatrix * patchVertices[2]).xyz;

    #if 0 //  human-readable version
    vec3 edgeCenter = (v0 + v2); // division by 2 was moved to globals.lodFactor
    vec3 edgeVector = (v2 - v0);
    float distanceToEdgeSqr = dot(edgeCenter, edgeCenter);
    float edgeLengthSqr = dot(edgeVector, edgeVector);

    return globals.lodFactor + log2(edgeLengthSqr / distanceToEdgeSqr);
    #else // optimized version
    float sqrMagSum = dot(v0, v0) + dot(v2, v2);
    float twoDotAC = 2.0f * dot(v0, v2);
    float distanceToEdgeSqr = sqrMagSum + twoDotAC;
    float edgeLengthSqr     = sqrMagSum - twoDotAC;

    return globals.lodFactor + log2(edgeLengthSqr / distanceToEdgeSqr);
    #endif
}

/*
    In Orthographic Mode, we have
        EdgePixelLength = EdgeViewSpaceLength / ImagePlaneViewSize * ImagePlanePixelResolution
    and so using some identities we get:
        LoD = 2 * (log2(EdgeViewSpaceLength)
            + log2(ImagePlanePixelResolution / ImagePlaneViewSize)
            - log2(TargetPixelLength))

            = log2(EdgeViewSpaceLength^2)
            + 2 * log2(ImagePlanePixelResolution / (ImagePlaneViewSize * TargetPixelLength))
    so we precompute:
    globals.lodFactor = 2 * log2(ImagePlanePixelResolution / (ImagePlaneViewSize * TargetPixelLength))
*/
float TriangleLevelOfDetail_Orthographic(in const vec4[3] patchVertices)
{
    vec3 v0 = (globals.modelViewMatrix * patchVertices[0]).xyz;
    vec3 v2 = (globals.modelViewMatrix * patchVertices[2]).xyz;
    vec3 edgeVector = (v2 - v0);
    float edgeLengthSqr = dot(edgeVector, edgeVector);

    return globals.lodFactor + log2(edgeLengthSqr);
}

float TriangleLevelOfDetail_Fisheye(in const vec4[3] patchVertices)
{
    vec3 v0 = (globals.modelViewMatrix * patchVertices[0]).xyz;
    vec3 v2 = (globals.modelViewMatrix * patchVertices[2]).xyz;
    vec3 edgeVector = (v2 - v0);
    float edgeLengthSqr = dot(edgeVector, edgeVector);

    return globals.lodFactor + log2(edgeLengthSqr);
}

float TriangleLevelOfDetail(in const vec4[3] patchVertices) {
    switch(projection_method){
        case PROJECTION_RECTILINEAR:
            return TriangleLevelOfDetail_Perspective(patchVertices);
        case PROJECTION_ORTHOGRAPHIC:
            return TriangleLevelOfDetail_Orthographic(patchVertices);
        case PROJECTION_FISHEYE:
            return TriangleLevelOfDetail_Fisheye(patchVertices);
        default:
            return 0.0;
    }
}

/*******************************************************************************
 * DisplacementVarianceTest -- Checks if the height variance criteria is met
 *
 * Terrains tend to have locally flat regions, which don't need large amounts
 * of polygons to be represented faithfully. This function checks the
 * local flatness of the terrain.
 *
 */
bool DisplacementVarianceTest(in const vec4[3] patchVertices) {
    vec2 P0 = patchVertices[0].xy;
    vec2 P1 = patchVertices[1].xy;
    vec2 P2 = patchVertices[2].xy;
    vec2 P = (P0 + P1 + P2) / 3.0;
    vec2 dx = (P0 - P1);
    vec2 dy = (P2 - P1);
    vec2 dmap = textureGrad(u_DmapSampler, P, dx, dy).rg;
    float dmapVariance = clamp(dmap.y - dmap.x * dmap.x, 0.0, 1.0);

    return (dmapVariance >= globals.minLodVariance);

}

/*******************************************************************************
 * LevelOfDetail -- Computes the level of detail of associated to a triangle
 *
 * The first component is the actual LoD value. The second value is 0 if the
 * triangle is culled, and one otherwise.
 *
 */
vec2 LevelOfDetail(in const vec4[3] patchVertices)
{
    // culling test
    if (!FrustumCullingTest(patchVertices)) {
        return should_cull_triangle ? vec2(0.0f, 0.0f) : vec2(0.0f, 1.0f);
    }

    if(should_displace && !DisplacementVarianceTest(patchVertices)) return vec2(0.0f, 1.0f);

    // compute triangle LOD
    return vec2(TriangleLevelOfDetail(patchVertices), 1.0f);
}

/*******************************************************************************
 * BarycentricInterpolation -- Computes a barycentric interpolation
 *
 */
vec2 BarycentricInterpolation(in vec2 v[3], in vec2 u){
    return v[1] + u.x * (v[2] - v[1]) + u.y * (v[0] - v[1]);
}

vec4 BarycentricInterpolation(in vec4 v[3], in vec2 u){
    return v[1] + u.x * (v[2] - v[1]) + u.y * (v[0] - v[1]);
}


/*******************************************************************************
 * GenerateVertex -- Computes the final vertex position
 *
 */
struct VertexAttribute {
    vec4 position;
    vec2 texCoord;
};

VertexAttribute TessellateTriangle(in const vec2 texCoords[3], in vec2 tessCoord) {
    vec2 texCoord = BarycentricInterpolation(texCoords, tessCoord);
    vec4 position = vec4(texCoord, 0, 1);

//    vec2 uv = vec2(texCoord.x, 1 - texCoord.y);
    vec2 uv = vec2(texCoord.x, texCoord.y);
    if(should_displace) {
        position.z = globals.dmapFactor * textureLod(u_DmapSampler, uv, 0.0).r;
    }

    return VertexAttribute(position, uv);
}

vec3 depthToNormal(sampler2D depth_map, vec2 uv) {
    float bump_strength = 2;
    float heightL = texture(depth_map, uv + vec2(-1.0, 0.0) / textureSize(depth_map, 0)).r;
    float heightR = texture(depth_map, uv + vec2(1.0, 0.0) / textureSize(depth_map, 0)).r;
    float heightD = texture(depth_map, uv + vec2(0.0, -1.0) / textureSize(depth_map, 0)).r;
    float heightU = texture(depth_map, uv + vec2(0.0, 1.0) / textureSize(depth_map, 0)).r;

    // Calculate the gradients (dx, dy) with added bump strength factor
    float dx = (heightR - heightL) * bump_strength;
    float dy = (heightU - heightD) * bump_strength;

    vec3 normal = normalize(vec3(-dx, -dy, 1.0));

    return 0.5 + 0.5 * normal;
}

vec3 depthToNormal1(sampler2D depth_map, vec2 uv) {
    float filterSize = 1.0f / float(textureSize(depth_map, 0).x);// sqrt(dot(dFdx(texCoord), dFdy(texCoord)));
    float sx0 = textureLod(depth_map, uv - vec2(filterSize, 0.0), 0.0).r;
    float sx1 = textureLod(depth_map, uv + vec2(filterSize, 0.0), 0.0).r;
    float sy0 = textureLod(depth_map, uv - vec2(0.0, filterSize), 0.0).r;
    float sy1 = textureLod(depth_map, uv + vec2(0.0, filterSize), 0.0).r;
    float sx = sx1 - sx0;
    float sy = sy1 - sy0;

    return vec3(globals.dmapFactor * 0.03 / filterSize * 0.5f * vec2(-sx, -sy), 1);
}



#endif // TERRAIN_SHARED_GLSL