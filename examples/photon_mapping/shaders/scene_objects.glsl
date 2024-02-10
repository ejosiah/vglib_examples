#ifndef SCENE_OBJECTS_GLSL
#define SCENE_OBJECTS_GLSL

struct Material{
    vec3 diffuse;
    vec3 ambient;
    vec3 specular;
    vec3 emission;
    vec3 transmittance;
    float shininess;
    float ior;
    float opacity;
    float illum;
};

struct VertexOffsets{
    int firstIndex;
    int vertexOffset;
    int material;
    int padding1;
};

struct Vertex{
    vec3 position;
    vec3 color;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec2 uv;
};

struct SceneObject{
    mat4 xform;
    mat4 xformIT;
    int objId;
    int padding0;
    int padding1;
    int padding2;
};

layout(set = 1, binding = 0) buffer MATERIALS{
    Material m[];
} materials[];

layout(set = 1, binding = 1) buffer MATERIAL_ID {
    int i[];
} matIds [];

layout(set = 1, binding = 2) buffer OBJECT_INSTANCE {
    SceneObject sceneObjs[];  // TODO gl_InstanceCustomIndexEXT
}; // TODO per instance


layout(set = 2, binding = 0) buffer VERTEX_BUFFER {
    Vertex v[];   // TODO VertexOffsets.vertexOffset + i[VertexOffsets.firstIndex + gl_PrimitiveID]
} vertices[]; // TODO use object_id

layout(set = 2, binding = 1) buffer INDEX_BUFFER {
    int i[]; // TODO VertexOffsets.firstIndex + gl_PrimitiveID
} indices[]; // TODO use object_id

layout(set = 2, binding = 2) buffer VETEX_OFFSETS {
    VertexOffsets vo[]; // TODO use gl_InstanceCustomIndexEXT
} offsets[];  // TODO use object_id


struct HitData {
    Material material;
    vec3 position;
    vec3 normal; // shading normal
};

/*
    ref.attribs = attribs;
    ref.instanceId = gl_InstanceID;
    ref.vertexOffsetId = gl_InstanceCustomIndex;
    ref.primitiveId = gl_PrimitiveID;*/

HitData getHitData(vec2 attribs) {
    SceneObject sceneObj = sceneObjs[gl_InstanceID];
    int objId = sceneObj.objId;

    VertexOffsets offset = offsets[objId].vo[gl_InstanceCustomIndexEXT];

    ivec3 index = ivec3(
    indices[objId].i[offset.firstIndex + 3 * gl_PrimitiveID + 0],
    indices[objId].i[offset.firstIndex + 3 * gl_PrimitiveID + 1],
    indices[objId].i[offset.firstIndex + 3 * gl_PrimitiveID + 2]
    );

    Vertex v0 = vertices[objId].v[index.x + offset.vertexOffset];
    Vertex v1 = vertices[objId].v[index.y + offset.vertexOffset];
    Vertex v2 = vertices[objId].v[index.z + offset.vertexOffset];



    float u = 1 - attribs.x - attribs.y;
    float v = attribs.x;
    float w = attribs.y;

    HitData hitData;
    hitData.normal = v0.normal * u + v1.normal * v + v2.normal * w;
    int matId = matIds[objId].i[gl_PrimitiveID + offset.material];
    hitData.material = materials[objId].m[matId];

    hitData.position = v0.position * u + v1.position * v + v2.position * w;

    return hitData;
}

#endif // SCENE_OBJECTS_GLSL