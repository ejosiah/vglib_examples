//*****************************************************************************
//swizzle.glsl*****************************************************************
//*****************************************************************************

#ifndef SWIZZLE_GLSL
#define SWIZZLE_GLSL

//Lookup up table of permutations matrices used to reverse swizzling
const mat3 unswizzleLUT[] = { mat3(0,1,0,0,0,1,1,0,0), mat3(0,0,1,1,0,0,0,1,0), mat3(1,0,0,0,1,0,0,0,1) };

//swizzle triangle vertices
void swizzleTri(inout vec3 v0, inout vec3 v1, inout vec3 v2, out vec3 n, out mat3 unswizzle)
{
    //       cross(e0, e1);
    n = cross(v1 - v0, v2 - v1);

    vec3 absN = abs(n);
    float maxAbsN = max(max(absN.x, absN.y), absN.z);

    if(absN.x >= absN.y && absN.x >= absN.z)			//X-direction dominant (YZ-plane)
    {													//Then you want to look down the X-direction
        v0.xyz = v0.yzx;
        v1.xyz = v1.yzx;
        v2.xyz = v2.yzx;

        n.xyz = n.yzx;

        //XYZ <-> YZX
        unswizzle = unswizzleLUT[0];
    }
    else if(absN.y >= absN.x && absN.y >= absN.z)		//Y-direction dominant (ZX-plane)
    {													//Then you want to look down the Y-direction
        v0.xyz = v0.zxy;
        v1.xyz = v1.zxy;
        v2.xyz = v2.zxy;

        n.xyz = n.zxy;

        //XYZ <-> ZXY
        unswizzle = unswizzleLUT[1];
    }
    else												//Z-direction dominant (XY-plane)
    {													//Then you want to look down the Z-direction (the default)
        v0.xyz = v0.xyz;
        v1.xyz = v1.xyz;
        v2.xyz = v2.xyz;

        n.xyz = n.xyz;

        //XYZ <-> XYZ
        unswizzle = unswizzleLUT[2];
    }
}

//swizzle triangle vertices
void swizzleTri(inout vec3 v0, inout vec3 v1, inout vec3 v2, out vec3 n, out int domAxis)
{
    //       cross(e0, e1);
    n = cross(v1 - v0, v2 - v1);

    vec3 absN = abs(n);
    float maxAbsN = max(max(absN.x, absN.y), absN.z);

    if(absN.x >= absN.y && absN.x >= absN.z)			//X-direction dominant (YZ-plane)
    {													//Then you want to look down the X-direction
        v0.xyz = v0.yzx;
        v1.xyz = v1.yzx;
        v2.xyz = v2.yzx;

        n.xyz = n.yzx;

        //XYZ <-> YZX
        domAxis = 1;
    }
    else if(absN.y >= absN.x && absN.y >= absN.z)		//Y-direction dominant (ZX-plane)
    {													//Then you want to look down the Y-direction
        v0.xyz = v0.zxy;
        v1.xyz = v1.zxy;
        v2.xyz = v2.zxy;

        n.xyz = n.zxy;

        //XYZ <-> ZXY
        domAxis = 0;
    }
    else												//Z-direction dominant (XY-plane)
    {													//Then you want to look down the Z-direction (the default)
        v0.xyz = v0.xyz;
        v1.xyz = v1.xyz;
        v2.xyz = v2.xyz;

        n.xyz = n.xyz;

        //XYZ <-> XYZ
        domAxis = 2;
    }
}

#endif //SWIZZLE_GLSL
