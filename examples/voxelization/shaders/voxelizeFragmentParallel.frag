//*****************************************************************************
//voxelizeFragmentParallel.frag************************************************
//*****************************************************************************

#version 460

#define L     0.7071067811865475244008443621048490392848359376884740
#define L_SQR 0.5

#define THIN 0 	//6-separating
#define FAT  1	//26-separating
layout(constant_id = 0) const int THICKNESS = 0;

layout(set = 0, binding = 0, r32ui) uniform uimage3D Voxels;

layout(location = 0) in struct {
    vec3 position;
    vec3 normal;
    vec2 uv;
} fs_in;

// OUT voxels extents snapped to voxel grid (post swizzle)
layout(location = 3) flat in ivec3 minVoxIndex;
layout(location = 4) flat in ivec3 maxVoxIndex;

// OUT 2D projected edge normals
layout(location = 5) flat in vec2 n_e0_xy;
layout(location = 6) flat in vec2 n_e1_xy;
layout(location = 7) flat in vec2 n_e2_xy;
layout(location = 8) flat in vec2 n_e0_yz;
layout(location = 9) flat in vec2 n_e1_yz;
layout(location = 10) flat in vec2 n_e2_yz;
layout(location = 11) flat in vec2 n_e0_zx;
layout(location = 12) flat in vec2 n_e1_zx;
layout(location = 13) flat in vec2 n_e2_zx;

// OUT
layout(location = 14) flat in float d_e0_xy;
layout(location = 15) flat in float d_e1_xy;
layout(location = 16) flat in float d_e2_xy;
layout(location = 17) flat in float d_e0_yz;
layout(location = 18) flat in float d_e1_yz;
layout(location = 19) flat in float d_e2_yz;
layout(location = 20) flat in float d_e0_zx;
layout(location = 21) flat in float d_e1_zx;
layout(location = 22) flat in float d_e2_zx;

// OUT pre-calculated triangle intersection stuff
layout(location = 23) flat in vec3  nProj;
// #if THICKNESS == THIN
layout(location = 24) flat in float dTriThin;
// #elif THICKNESS == FAT
layout(location = 25) flat in float dTriFatMin;
layout(location = 26) flat in float dTriFatMax;
// #endif
layout(location = 27) flat in float nzInv;

layout(location = 28) flat in int Z;


void writeVoxels(ivec3 coord, uint val){
    //modify as necessary for attributes/storage type
    imageStore(Voxels, coord, uvec4(val));
}

layout(location = 0) out vec4 fragColor;

void main()
{
    if(any(greaterThan(fs_in.position, maxVoxIndex)) || any(lessThan(fs_in.position, minVoxIndex)))
    {
        discard;
    }

    ivec3 p = ivec3(fs_in.position);	//voxel coordinate (swizzled)
    int   zMin,      zMax;			//voxel Z-range
    float zMinInt,   zMaxInt;		//voxel Z-intersection min/max
    float zMinFloor, zMaxCeil;		//voxel Z-intersection floor/ceil

    float dd_e0_xy = d_e0_xy + dot(n_e0_xy, p.xy);
    float dd_e1_xy = d_e1_xy + dot(n_e1_xy, p.xy);
    float dd_e2_xy = d_e2_xy + dot(n_e2_xy, p.xy);

    bool xy_overlap = (dd_e0_xy >= 0) && (dd_e1_xy >= 0) && (dd_e2_xy >= 0);

    if(xy_overlap)	//figure 17 line 15, figure 18 line 14
    {
        float dot_n_p = dot(nProj.xy, p.xy);
        if(THICKNESS == THIN){
            zMinInt = (-dot_n_p + dTriThin) * nzInv;
            zMaxInt = zMinInt;
        } else if(THICKNESS == FAT) {
            zMinInt = (-dot_n_p + dTriFatMin) * nzInv;
            zMaxInt = (-dot_n_p + dTriFatMax) * nzInv;
        }
        zMinFloor = floor(zMinInt);
        zMaxCeil  =  ceil(zMaxInt);

        zMin = int(zMinFloor) - int(zMinFloor == zMinInt);
        zMax = int(zMaxCeil ) + int(zMaxCeil  == zMaxInt);

        zMin = max(minVoxIndex.z, zMin);	//clamp to bounding box max Z
        zMax = min(maxVoxIndex.z, zMax);	//clamp to bounding box min Z

        for(p.z = zMin; p.z < zMax; p.z++)	//figure 17/18 line 18
        {
            float dd_e0_yz = d_e0_yz + dot(n_e0_yz, p.yz);
            float dd_e1_yz = d_e1_yz + dot(n_e1_yz, p.yz);
            float dd_e2_yz = d_e2_yz + dot(n_e2_yz, p.yz);

            float dd_e0_zx = d_e0_zx + dot(n_e0_zx, p.zx);
            float dd_e1_zx = d_e1_zx + dot(n_e1_zx, p.zx);
            float dd_e2_zx = d_e2_zx + dot(n_e2_zx, p.zx);

            bool yz_overlap = (dd_e0_yz >= 0) && (dd_e1_yz >= 0) && (dd_e2_yz >= 0);
            bool zx_overlap = (dd_e0_zx >= 0) && (dd_e1_zx >= 0) && (dd_e2_zx >= 0);

            if(yz_overlap && zx_overlap)	//figure 17/18 line 19
            {

                ivec3 origCoord = (Z == 0) ? p.yzx : (Z == 1) ? p.zxy : p.xyz;	//this actually slightly outperforms unswizzle

                writeVoxels(origCoord, 1);	//figure 17/18 line 20
                fragColor.rgb = vec3(1, 0, 0);
            }
        }
        //z-loop
    }
    //xy-overlap test

    discard;
}