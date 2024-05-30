//*****************************************************************************
//bary.glsl********************************************************************
//*****************************************************************************

#ifndef BARY_GLSL
#define BARY_GLSL

void computeBaryCoords(inout vec3 b0, inout vec3 b1, inout vec3 b2, vec2 v0, vec2 v1, vec2 v2)
{
    vec3 vb;
    float invDetA = 1.0 / ((v0.x*v1.y) - (v0.x*v2.y) - (v1.x*v0.y) + (v1.x*v2.y) + (v2.x*v0.y) - (v2.x*v1.y));

    vb.x = ( (b0.x*v1.y) - (b0.x*v2.y) - (v1.x*b0.y) + (v1.x*v2.y) + (v2.x*b0.y) - (v2.x*v1.y) ) * invDetA;
    vb.y = ( (v0.x*b0.y) - (v0.x*v2.y) - (b0.x*v0.y) + (b0.x*v2.y) + (v2.x*v0.y) - (v2.x*b0.y) ) * invDetA;
    vb.z = 1 - vb.x - vb.y;
    b0 = vb;

    vb.x = ( (b1.x*v1.y) - (b1.x*v2.y) - (v1.x*b1.y) + (v1.x*v2.y) + (v2.x*b1.y) - (v2.x*v1.y) ) * invDetA;
    vb.y = ( (v0.x*b1.y) - (v0.x*v2.y) - (b1.x*v0.y) + (b1.x*v2.y) + (v2.x*v0.y) - (v2.x*b1.y) ) * invDetA;
    vb.z = 1 - vb.x - vb.y;
    b1 = vb;

    vb.x = ( (b2.x*v1.y) - (b2.x*v2.y) - (v1.x*b2.y) + (v1.x*v2.y) + (v2.x*b2.y) - (v2.x*v1.y) ) * invDetA;
    vb.y = ( (v0.x*b2.y) - (v0.x*v2.y) - (b2.x*v0.y) + (b2.x*v2.y) + (v2.x*v0.y) - (v2.x*b2.y) ) * invDetA;
    vb.z = 1 - vb.x - vb.y;
    b2 = vb;
}

#endif //BARY_GLSL
