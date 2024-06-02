//*****************************************************************************
//voxelizeFragmentParallel.geom************************************************
//*****************************************************************************

#version 460

#define THIN 0	//6-separating
#define FAT  1	//26-separating
layout(constant_id = 0) const int THICKNESS = FAT;


#define AFTER             0
#define INLINE_BINARY     1
#define INLINE_ATTRIBUTES 2
layout(constant_id = 1) const int BUILD = AFTER;


#define L     0.7071067811865475244008443621048490392848359376884740	//sqrt(2)/2
#define L_SQR 0.5

#include "swizzle.glsl"
#include "bary.glsl"


layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(set = 0, binding = 0, r32ui) uniform uimage3D Voxels;

layout(push_constant) uniform Cosntants {
    mat4 modelTransform;
    mat4 orthoMatrix;
};


layout(location = 0) in struct {
    vec3 position;
    vec3 normal;
    vec3 color;
    vec2 uv;
} gs_in[];


layout(location = 0) out struct {
    vec3 position;
    vec3 normal;
    vec2 uv;
} gs_out;

// OUT voxels extents snapped to voxel grid (post swizzle)
layout(location = 3) flat out ivec3 minVoxIndex;
layout(location = 4) flat out ivec3 maxVoxIndex;

// OUT 2D projected edge normals
layout(location = 5) flat out vec2 n_e0_xy;
layout(location = 6) flat out vec2 n_e1_xy;
layout(location = 7) flat out vec2 n_e2_xy;
layout(location = 8) flat out vec2 n_e0_yz;
layout(location = 9) flat out vec2 n_e1_yz;
layout(location = 10) flat out vec2 n_e2_yz;
layout(location = 11) flat out vec2 n_e0_zx;
layout(location = 12) flat out vec2 n_e1_zx;
layout(location = 13) flat out vec2 n_e2_zx;

// OUT
layout(location = 14) flat out float d_e0_xy;
layout(location = 15) flat out float d_e1_xy;
layout(location = 16) flat out float d_e2_xy;
layout(location = 17) flat out float d_e0_yz;
layout(location = 18) flat out float d_e1_yz;
layout(location = 19) flat out float d_e2_yz;
layout(location = 20) flat out float d_e0_zx;
layout(location = 21) flat out float d_e1_zx;
layout(location = 22) flat out float d_e2_zx;

// OUT pre-calculated triangle intersection stuff
layout(location = 23) flat out vec3  nProj;
// #if THICKNESS == THIN
layout(location = 24) flat out float dTriThin;
// #elif THICKNESS == FAT
layout(location = 25) flat out float dTriFatMin;
layout(location = 26) flat out float dTriFatMax;
// #endif
layout(location = 27) flat out float nzInv;

layout(location = 28) flat out int Z;


void main()
{
    vec3 v0 = gs_in[0].position;
    vec3 v1 = gs_in[1].position;
    vec3 v2 = gs_in[2].position;

    vec3 n;
    swizzleTri(v0, v1, v2, n, Z);

    vec3 AABBmin = min(min(v0, v1), v2);
    vec3 AABBmax = max(max(v0, v1), v2);

    ivec3 volumeDim = imageSize(Voxels);

    minVoxIndex = ivec3(clamp(floor(AABBmin), ivec3(0), volumeDim));
    maxVoxIndex = ivec3(clamp( ceil(AABBmax), ivec3(0), volumeDim));

    //Edges for swizzled vertices;
    vec3 e0 = v1 - v0;	//figure 17/18 line 2
    vec3 e1 = v2 - v1;	//figure 17/18 line 2
    vec3 e2 = v0 - v2;	//figure 17/18 line 2

    //INward Facing edge normals XY
    n_e0_xy = (n.z >= 0) ? vec2(-e0.y, e0.x) : vec2(e0.y, -e0.x);	//figure 17/18 line 4
    n_e1_xy = (n.z >= 0) ? vec2(-e1.y, e1.x) : vec2(e1.y, -e1.x);	//figure 17/18 line 4
    n_e2_xy = (n.z >= 0) ? vec2(-e2.y, e2.x) : vec2(e2.y, -e2.x);	//figure 17/18 line 4

    //INward Facing edge normals YZ
    n_e0_yz = (n.x >= 0) ? vec2(-e0.z, e0.y) : vec2(e0.z, -e0.y);	//figure 17/18 line 5
    n_e1_yz = (n.x >= 0) ? vec2(-e1.z, e1.y) : vec2(e1.z, -e1.y);	//figure 17/18 line 5
    n_e2_yz = (n.x >= 0) ? vec2(-e2.z, e2.y) : vec2(e2.z, -e2.y);	//figure 17/18 line 5

    //INward Facing edge normals ZX
    n_e0_zx = (n.y >= 0) ? vec2(-e0.x, e0.z) : vec2(e0.x, -e0.z);	//figure 17/18 line 6
    n_e1_zx = (n.y >= 0) ? vec2(-e1.x, e1.z) : vec2(e1.x, -e1.z);	//figure 17/18 line 6
    n_e2_zx = (n.y >= 0) ? vec2(-e2.x, e2.z) : vec2(e2.x, -e2.z);	//figure 17/18 line 6

    if(THICKNESS == THIN){
        d_e0_xy = dot(n_e0_xy, .5-v0.xy) + 0.5 * max(abs(n_e0_xy.x), abs(n_e0_xy.y));//figure 18 line 7
        d_e1_xy = dot(n_e1_xy, .5-v1.xy) + 0.5 * max(abs(n_e1_xy.x), abs(n_e1_xy.y));//figure 18 line 7
        d_e2_xy = dot(n_e2_xy, .5-v2.xy) + 0.5 * max(abs(n_e2_xy.x), abs(n_e2_xy.y));//figure 18 line 7

        d_e0_yz = dot(n_e0_yz, .5-v0.yz) + 0.5 * max(abs(n_e0_yz.x), abs(n_e0_yz.y));//figure 18 line 8
        d_e1_yz = dot(n_e1_yz, .5-v1.yz) + 0.5 * max(abs(n_e1_yz.x), abs(n_e1_yz.y));//figure 18 line 8
        d_e2_yz = dot(n_e2_yz, .5-v2.yz) + 0.5 * max(abs(n_e2_yz.x), abs(n_e2_yz.y));//figure 18 line 8

        d_e0_zx = dot(n_e0_zx, .5-v0.zx) + 0.5 * max(abs(n_e0_zx.x), abs(n_e0_zx.y));//figure 18 line 9
        d_e1_zx = dot(n_e1_zx, .5-v1.zx) + 0.5 * max(abs(n_e1_zx.x), abs(n_e1_zx.y));//figure 18 line 9
        d_e2_zx = dot(n_e2_zx, .5-v2.zx) + 0.5 * max(abs(n_e2_zx.x), abs(n_e2_zx.y));//figure 18 line 9
    } else if(THICKNESS == FAT) {
        d_e0_xy = -dot(n_e0_xy, v0.xy) + max(0.0f, n_e0_xy.x) + max(0.0f, n_e0_xy.y);//figure 17 line 7
        d_e1_xy = -dot(n_e1_xy, v1.xy) + max(0.0f, n_e1_xy.x) + max(0.0f, n_e1_xy.y);//figure 17 line 7
        d_e2_xy = -dot(n_e2_xy, v2.xy) + max(0.0f, n_e2_xy.x) + max(0.0f, n_e2_xy.y);//figure 17 line 7

        d_e0_yz = -dot(n_e0_yz, v0.yz) + max(0.0f, n_e0_yz.x) + max(0.0f, n_e0_yz.y);//figure 17 line 8
        d_e1_yz = -dot(n_e1_yz, v1.yz) + max(0.0f, n_e1_yz.x) + max(0.0f, n_e1_yz.y);//figure 17 line 8
        d_e2_yz = -dot(n_e2_yz, v2.yz) + max(0.0f, n_e2_yz.x) + max(0.0f, n_e2_yz.y);//figure 17 line 8

        d_e0_zx = -dot(n_e0_zx, v0.zx) + max(0.0f, n_e0_zx.x) + max(0.0f, n_e0_zx.y);//figure 18 line 9
        d_e1_zx = -dot(n_e1_zx, v1.zx) + max(0.0f, n_e1_zx.x) + max(0.0f, n_e1_zx.y);//figure 18 line 9
        d_e2_zx = -dot(n_e2_zx, v2.zx) + max(0.0f, n_e2_zx.x) + max(0.0f, n_e2_zx.y);//figure 18 line 9
    }

    nProj = (n.z < 0.0) ? -n : n;	//figure 17/18 line 10

    const float dTri = dot(nProj, v0);

    if(THICKNESS == THIN){
        dTriThin   = dTri - dot(nProj.xy, vec2(0.5));//figure 18 line 11
    } else if(THICKNESS == FAT) {
        dTriFatMin = dTri - max(nProj.x, 0) - max(nProj.y, 0);//figure 17 line 11
        dTriFatMax = dTri - min(nProj.x, 0) - min(nProj.y, 0);//figure 17 line 12
    }

    nzInv = 1.0 / nProj.z;

    vec2 n_e0_xy_norm = -normalize(n_e0_xy);	//edge normals must be normalized for expansion, and made OUTward facing
    vec2 n_e1_xy_norm = -normalize(n_e1_xy);	//edge normals must be normalized for expansion, and made OUTward facing
    vec2 n_e2_xy_norm = -normalize(n_e2_xy);	//edge normals must be normalized for expansion, and made OUTward facing

    vec3 v0_prime, v1_prime, v2_prime;
    v0_prime.xy = v0.xy + L * ( e2.xy / ( dot(e2.xy, n_e0_xy_norm) ) + e0.xy / ( dot(e0.xy, n_e2_xy_norm) ) );
    v1_prime.xy = v1.xy + L * ( e0.xy / ( dot(e0.xy, n_e1_xy_norm) ) + e1.xy / ( dot(e1.xy, n_e0_xy_norm) ) );
    v2_prime.xy = v2.xy + L * ( e1.xy / ( dot(e1.xy, n_e2_xy_norm) ) + e2.xy / ( dot(e2.xy, n_e1_xy_norm) ) );

    v0_prime.z = (-dot(nProj.xy, v0_prime.xy) + dTri) * nzInv;
    v1_prime.z = (-dot(nProj.xy, v1_prime.xy) + dTri) * nzInv;
    v2_prime.z = (-dot(nProj.xy, v2_prime.xy) + dTri) * nzInv;

    vec3 b0, b1, b2;
    if(BUILD == INLINE_ATTRIBUTES){
        b0 = v0_prime;
        b1 = v1_prime;
        b2 = v2_prime;
        computeBaryCoords(b0, b1, b2, v0.xy, v1.xy, v2.xy);
    };

    gl_Position      = orthoMatrix * vec4(v0_prime,1);
    gs_out.position    = v0_prime;

    if(BUILD == INLINE_ATTRIBUTES){
        gs_out.normal = b0.x*gs_in[0].normal + b0.y*gs_in[1].normal + b0.z*gs_in[2].normal;
        gs_out.uv     = b0.x*gs_in[0].uv     + b0.y*gs_in[1].uv     + b0.z*gs_in[2].uv;
    }
    EmitVertex();

    gl_Position      = orthoMatrix * vec4(v1_prime,1);
    gs_out.position    = v1_prime;
    if(BUILD == INLINE_ATTRIBUTES){
        gs_out.normal = b1.x*gs_in[0].normal + b1.y*gs_in[1].normal + b1.z*gs_in[2].normal;
        gs_out.uv     = b1.x*gs_in[0].uv     + b1.y*gs_in[1].uv     + b1.z*gs_in[2].uv;
    }
    EmitVertex();

    gl_Position      = orthoMatrix * vec4(v2_prime,1);
    gs_out.position    = v2_prime;
    if(BUILD == INLINE_ATTRIBUTES){
        gs_out.normal = b2.x*gs_in[0].normal + b2.y*gs_in[1].normal + b2.z*gs_in[2].normal;
        gs_out.uv     = b2.x*gs_in[0].uv     + b2.y*gs_in[1].uv     + b2.z*gs_in[2].uv;
    }
    EmitVertex();

    EndPrimitive();
}