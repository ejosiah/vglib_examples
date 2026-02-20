#ifndef GLTF_MODEL_RT_GLSL
#define GLTF_MODEL_RT_GLSL

#ifndef MODEL_RT_SET
#define MODEL_RT_SET 0
#endif // MODEL_RT_SET

#include "model.glsl"

#define INDEX_TYPE_U8 0
#define INDEX_TYPE_U16 1
#define INDEX_TYPE_U32 2

struct Vertex {
    vec4 position;
    vec4 color0;
    vec4 color1;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec4 uv;
};

struct VertexOffset {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};


struct InstanceData {
    Mesh mesh;
    Material material;
    vec4 position;
    vec4 color0;
    vec4 color1;
    vec4 uv;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;

};

layout(set = MODEL_RT_SET, binding = 1, scalar) buffer MODEL_VERTEX {
    Vertex vertices[];
};

layout(set = MODEL_RT_SET, binding = 2, scalar) buffer MODEL_INDEXES {
    uint i[];
} indexes[3];

layout(set = MODEL_RT_SET, binding = 3, scalar) buffer INSTANCE_MESH {
    Mesh m[];
} meshes[3];

layout(set = MODEL_RT_SET, binding = 4, scalar) buffer DRAW_INFO {
    VertexOffset o[];
} offsets[3];

layout(set = MODEL_RT_SET, binding = 5) buffer GLTF_MATERIAL {
    Material materials[];
};

layout(set = MODEL_RT_SET, binding = 6) buffer TextureInfos {
    TextureInfo textureInfos[];
};

uvec3 extractIndices(uint firstIndex, uint primitiveIndex, uint indexType, uint indexTypeSize) {
    uint divisor = 4/indexTypeSize;
    uint wordSize = 8 * indexTypeSize;
    uint mask = (1 << wordSize) - 1u;

    uint wordIndex = firstIndex + primitiveIndex * 3;
    uint i = wordIndex/divisor;

    uvec3 res;
    res.x = (indexes[indexType].i[i] >> (wordIndex * wordSize)) & mask;

    wordIndex++;
    i = (wordIndex)/divisor;
    res.y = (indexes[indexType].i[i] >> (wordIndex * wordSize)) & mask;

    wordIndex++;
    i = (wordIndex)/divisor;
    res.z = (indexes[indexType].i[i] >> (wordIndex * wordSize)) & mask;
    return res;
}

uvec3 getPrimitive(uint firstIndex, uint  primitiveIndex, uint indexType) {
    switch(indexType) {
        case INDEX_TYPE_U8:
            return extractIndices(firstIndex, primitiveIndex, indexType, 1);
        case INDEX_TYPE_U16:
            return extractIndices(firstIndex, primitiveIndex, indexType, 2);
        default: {
            uvec3 primitive;
            primitive.x = indexes[indexType].i[firstIndex + primitiveIndex * 3];
            primitive.y = indexes[indexType].i[firstIndex + primitiveIndex * 3 + 1];
            primitive.z = indexes[indexType].i[firstIndex + primitiveIndex * 3 + 2];
            return primitive;
        }

    }
}

void load(out InstanceData instance, vec2 bc, uint customIndex, uint  primitiveIndex) {
    uint instanceIndex = (customIndex >> 2) & 0x3FFFFFu;
    uint indexType = customIndex & 0x3u;

    VertexOffset offset = offsets[indexType].o[instanceIndex];

    uvec3 primitive = getPrimitive(offset.firstIndex, primitiveIndex, indexType);

    Vertex v0 = vertices[primitive.x + offset.vertexOffset];
    Vertex v1 = vertices[primitive.y + offset.vertexOffset];
    Vertex v2 = vertices[primitive.z + offset.vertexOffset];

    float u = 1 - bc.x - bc.y;
    float v = bc.x;
    float w = bc.y;

    instance.position = v0.position * u + v1.position * v + v2.position * w;
    instance.normal = normalize(v0.normal * u + v1.normal * v + v2.normal * w);
    instance.tangent = normalize(v0.tangent * u + v1.tangent * v + v2.tangent * w);
    instance.bitangent = normalize(v0.bitangent * u + v1.bitangent * v + v2.bitangent * w);
    instance.color0 = v0.color0 * u + v1.color0 * v + v2.color0 * w;
    instance.color1 = v0.color1 * u + v1.color1 * v + v2.color1 * w;
    instance.uv = v0.uv * u + v1.uv * v + v2.uv * w;


    instance.mesh = meshes[indexType].m[instanceIndex];
    instance.material = materials[instance.mesh.materialId];

    mat4 transform = instance.mesh.model;
    mat3 nModel = transpose(inverse(mat3(transform)));

    instance.position = transform * instance.position;
    instance.normal = normalize(nModel * instance.normal);
    instance.tangent = normalize(nModel * instance.tangent);
    instance.bitangent = normalize(nModel * instance.bitangent);
}


#endif // GLTF_MODEL_RT_GLSL
