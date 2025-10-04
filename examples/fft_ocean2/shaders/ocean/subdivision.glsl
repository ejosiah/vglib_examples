#ifndef OCEAN_SUBDIVISION_GLSL
#define OCEAN_SUBDIVISION_GLSL

#extension GL_EXT_nonuniform_qualifier : enable

#define CBT_HEAP_BUFFER_BINDING 0
#include "leb.glsl"
#include "frustum_culling.glsl"
#include "uniforms.glsl"

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

const uint flag_displace = 0;
const uint projection_method = PROJECTION_RECTILINEAR;
const uint cull_triangle = 1;

const bool should_displace = flag_displace == 1;
const bool should_cull_triangle = cull_triangle == 1;

bool FrustumCullingTest(in vec4[3] patchVertices) {

    patchVertices[0] = u.modelMatrix * patchVertices[0];
    patchVertices[1] = u.modelMatrix * patchVertices[1];
    patchVertices[2] = u.modelMatrix * patchVertices[2];

    patchVertices[0].xyz += sampleDisplacement(patchVertices[0].xy);
    patchVertices[1].xyz += sampleDisplacement(patchVertices[1].xy);
    patchVertices[2].xyz += sampleDisplacement(patchVertices[2].xy);

    vec3 bmin = min(min(patchVertices[0].xyz, patchVertices[1].xyz), patchVertices[2].xyz);
    vec3 bmax = max(max(patchVertices[0].xyz, patchVertices[1].xyz), patchVertices[2].xyz);

    return FrustumCullingTest(u.frustumPlanes, bmin, bmax);
}

vec4[3] DecodeTriangleVertices(in const cbt_Node node)
{
    vec3 xPos = vec3(0, 0, 1), yPos = vec3(1, 0, 0);
    mat2x3 pos = leb_DecodeNodeAttributeArray_Square(node, mat2x3(xPos, yPos));
    vec4 p1 = vec4(pos[0][0], pos[1][0], 0.0, 1.0);
    vec4 p2 = vec4(pos[0][1], pos[1][1], 0.0, 1.0);
    vec4 p3 = vec4(pos[0][2], pos[1][2], 0.0, 1.0);

    if(should_displace) {
        p1.z = u.dmapFactor * texture(u_DmapSampler, vec3(p1.xy, u.tileCount)).r;
        p2.z = u.dmapFactor * texture(u_DmapSampler, vec3(p2.xy, u.tileCount)).r;
        p3.z = u.dmapFactor * texture(u_DmapSampler, vec3(p3.xy, u.tileCount)).r;
    }

    return vec4[3](p1, p2, p3);
}

float TriangleLevelOfDetail_Perspective(in const vec4[3] patchVertices)
{

    vec3 p0 = patchVertices[0].xyz;
    vec3 p2 = patchVertices[2].xyz;
    vec3 v0 = (u.modelViewMatrix * patchVertices[0]).xyz;
    vec3 v2 = (u.modelViewMatrix * patchVertices[2]).xyz;

    #if 0 //  human-readable version
    vec3 edgeCenter = (v0 + v2); // division by 2 was moved to u.lodFactor
    vec3 edgeVector = (v2 - v0);
    float distanceToEdgeSqr = dot(edgeCenter, edgeCenter);
    float edgeLengthSqr = dot(edgeVector, edgeVector);

    return u.lodFactor + log2(edgeLengthSqr / distanceToEdgeSqr);
    #else // optimized version
    float sqrMagSum = dot(v0, v0) + dot(v2, v2);
    float twoDotAC = 2.0f * dot(v0, v2);
    float distanceToEdgeSqr = sqrMagSum + twoDotAC;
    float edgeLengthSqr     = sqrMagSum - twoDotAC;

    return u.lodFactor + log2(edgeLengthSqr / distanceToEdgeSqr);
    #endif
}

float TriangleLevelOfDetail_Orthographic(in const vec4[3] patchVertices)
{
    vec3 v0 = (u.modelViewMatrix * patchVertices[0]).xyz;
    vec3 v2 = (u.modelViewMatrix * patchVertices[2]).xyz;
    vec3 edgeVector = (v2 - v0);
    float edgeLengthSqr = dot(edgeVector, edgeVector);

    return u.lodFactor + log2(edgeLengthSqr);
}

float TriangleLevelOfDetail_Fisheye(in const vec4[3] patchVertices)
{
    vec3 v0 = (u.modelViewMatrix * patchVertices[0]).xyz;
    vec3 v2 = (u.modelViewMatrix * patchVertices[2]).xyz;
    vec3 edgeVector = (v2 - v0);
    float edgeLengthSqr = dot(edgeVector, edgeVector);

    return u.lodFactor + log2(edgeLengthSqr);
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

vec2 LevelOfDetail(in const vec4[3] patchVertices) {
    // culling test
    if (!FrustumCullingTest(patchVertices)) {
        return should_cull_triangle ? vec2(0.0f, 0.0f) : vec2(0.0f, 1.0f);
    }

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

    vec3 uv = vec3(texCoord.x, texCoord.y, u.tileCount);
    if(should_displace) {
        position.z = u.dmapFactor * textureLod(u_DmapSampler, uv, 0.0).r;
    }

    return VertexAttribute(position, uv.xy);
}

#endif  // OCEAN_SUBDIVISION_GLSL