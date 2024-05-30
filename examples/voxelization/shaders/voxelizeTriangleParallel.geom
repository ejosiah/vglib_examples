//*****************************************************************************
//voxelizeTriangleParallel.geom************************************************
//*****************************************************************************

#version 460

#define THIN 0
#define FAT  1
#define THICKNESS THIN

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;


layout(location = 0) in struct {
    vec3 position;
    vec3 color;
} gs_in[];


#include "swizzle.glsl"
#include "voxelizeTriPostSwizzle.glsl"


void main()
{
    vec3 n;
    mat3 unswizzle;
    vec3 v0 = gs_in[0].position;
    vec3 v1 = gs_in[1].position;
    vec3 v2 = gs_in[2].position;

    swizzleTri(v0, v1, v2, n, unswizzle);

    vec3 AABBmin = min(min(v0, v1), v2);
    vec3 AABBmax = max(max(v0, v1), v2);

    ivec3 volumeDim = imageSize(Voxels);
    ivec3 minVoxIndex = ivec3(clamp(floor(AABBmin), ivec3(0), volumeDim));
    ivec3 maxVoxIndex = ivec3(clamp( ceil(AABBmax), ivec3(0), volumeDim));

    voxelizeTriPostSwizzle(v0, v1, v2, n, unswizzle, minVoxIndex, maxVoxIndex);
}
