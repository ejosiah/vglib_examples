#ifndef PLANET_COSNTANTS_GLSL
#define PLANET_COSNTANTS_GLSL


#ifndef PLANTE_SET
#define PLANTE_SET 0
#endif // PLANTE_SET
layout(set = PLANTE_SET, binding = 0, scalar) uniform GeometryUBO {
    uint _TotalNumElements;
    uint _BaseDepth;
    uint _TotalNumVertices;
    uint _MaterialID;
};

layout(set = PLANTE_SET, binding = 2, scalar) readonly buffer UpdateCB{
    mat4 _UpdateViewProjectionMatrix;
    mat4 _UpdateInvViewProjectionMatrix;
    vec4 _FrustumPlanes[6];
    vec3 _UpdateCameraPosition;
    vec3 _UpdateCameraForward;
    float _TriangleSize;
    uint _MaxSubdivisionDepth;
    float _UpdateFOV;
    float _UpdateFarPlaneDistance;
};

//layout(set = PLANTE_SET, binding = 1, scalar) uniform PlanetCB {
//    vec3 _PlanetCenter;
//    float _PlanetRadius;
//};



#endif // PLANET_COSNTANTS_GLSL