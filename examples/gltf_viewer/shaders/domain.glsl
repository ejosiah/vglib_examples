#ifndef GLTF_VIEWER_DOMAIN_GLSL
#define GLTF_VIEWER_DOMAIN_GLSL

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

#define INDEX_TYPE_U8 0
#define INDEX_TYPE_U16 1
#define INDEX_TYPE_U32 2

#include "ray_tracing_lang.glsl"

#include "gltf.glsl"
#include "octahedral.glsl"
#include "random.glsl"
#include "uniforms.glsl"
#include "sampling.glsl"
#include "rtx_utils.glsl"

#define render_target global_images[g_buffer_image_id]
#define enivornment_texture global_textures[environment];
#define FLT_MAX 3.402823466e+38F

struct HitRecord {
    vec3 brdfWeigth;
    vec3 Le;
    vec3 Ld;
    vec3 x;
    vec3 wo;
    vec3 wi;
    vec3 n;
    RngStateType rngState;
    bool hit;
};

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

vec2 sampleVec2(inout RngStateType state) {
    return vec2(rand(state), rand(state));
}

layout(set = 1, binding = 1, scalar) buffer MODEL_VERTEX {
    Vertex vertices[];
};

layout(set = 1, binding = 2, scalar) buffer MODEL_INDEXES {
    uint i[];
} indexes[3];

layout(set = 1, binding = 3, scalar) buffer INSTANCE_MESH {
    Mesh m[];
} meshes[3];

layout(set = 1, binding = 4, scalar) buffer DRAW_INFO {
    VertexOffset o[];
} offsets[3];

layout(set = 1, binding = 5) buffer GLTF_MATERIAL {
    Material materials[];
};

layout(set = 1, binding = 6) buffer TextureInfos {
    TextureInfo textureInfos[];
};

layout(set = 2, binding = 10) uniform sampler2D global_textures[];

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

#endif // GLTF_VIEWER_DOMAIN_GLSL