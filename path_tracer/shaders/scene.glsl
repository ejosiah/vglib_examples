#include "common.glsl"
#include "scene_push_constants.glsl"

layout(set = 1, binding = 0, std430) buffer MATERIALS{
    Material m[];
} materials[];


layout(set = 1, binding = 1) buffer MATERIAL_ID {
    int i[];
} matIds [];


layout(set = 1, binding = 2) buffer OBJECT_INSTANCE {
    SceneObject sceneObjs[];
};

layout(set = 1, binding = 3) buffer RECTANGLES{
    Rectangle rectangles[];
};

layout(set = 1, binding = 4) buffer DISK {
    Disk disks[];
};

layout(set = 1, binding = 5) buffer SPHERE {
    Sphere spheres[];
};

layout(set = 1, binding = 6) buffer LIGHT_SHAPE_REF{
    ShapeRef shapeRefs[];
};


layout(set = 2, binding = 0) buffer VERTEX_BUFFER {
    Vertex v[];
} vertices[];

layout(set = 2, binding = 1) buffer INDEX_BUFFER {
    int i[];
} indices[];

layout(set = 2, binding = 2) buffer VETEX_OFFSETS {
    VertexOffsets vo[];
} offsets[];

layout(set = 3, binding = 0) buffer LIGHTS {
    Light lights[];
};

void getTriangle(int objId, mat4x3 xform, uint offsetId, uint primitiveId, out Vertex v0, out Vertex v1, out Vertex v2){
    uint vertexOffsetId = 0;
    VertexOffsets offset = offsets[objId].vo[offsetId];

    ivec3 index = ivec3(
    indices[objId].i[offset.firstIndex + 3 * primitiveId + 0],
    indices[objId].i[offset.firstIndex + 3 * primitiveId + 1],
    indices[objId].i[offset.firstIndex + 3 * primitiveId + 2]
    );

    v0 = vertices[objId].v[index.x + offset.vertexOffset];
    v1 = vertices[objId].v[index.y + offset.vertexOffset];
    v2 = vertices[objId].v[index.z + offset.vertexOffset];

    v0.position = (xform * vec4(v0.position, 1)).xyz;
    v1.position = (xform * vec4(v1.position, 1)).xyz;
    v2.position = (xform * vec4(v2.position, 1)).xyz;

    mat3 nMatrix = inverse(transpose(mat3(xform)));

    v0.normal = nMatrix * v0.normal;
    v1.normal = nMatrix * v1.normal;
    v2.normal = nMatrix * v2.normal;
}

Surface getSurfaceData(SurfaceRef ref, vec3 wo){
    Surface surface;
    surface.id = ref.instanceId;
    surface.inside = false;
    surface.volume = false;
    Vertex v0, v1, v2;
    SceneObject sceneObj = sceneObjs[ref.instanceId];
    int objId = sceneObj.objId;
    getTriangle(objId, ref.objToWorld, ref.vertexOffsetId, ref.primitiveId, v0, v1, v2);
    float u = 1 - ref.attribs.x - ref.attribs.y;
    float v = ref.attribs.x;
    float w = ref.attribs.y;

    surface.x = u * v0.position + v * v1.position + w * v2.position;
    surface.sN = normalize(u * v0.normal + v * v1.normal + w * v2.normal);
    vec3 e0 = v1.position - v0.position;
    vec3 e1 = v2.position - v0.position;
    surface.gN = normalize(cross(e0, e1));

    VertexOffsets offset = offsets[objId].vo[ref.vertexOffsetId];
    int matId = matIds[objId].i[ref.primitiveId + offset.material];
    Material material = materials[objId].m[matId];
    surface.albedo = material.diffuse;
    surface.emission = material.emission;
    surface.metalness = material.metalness.x;
    surface.roughness = material.roughness;
    surface.opacity = material.opacity;

    if(ref.instanceId == planeId){
        surface.albedo = checkerboard(surface.x, surface.gN);
    }

    if(dot(surface.gN, wo) < 0){
        surface.inside = true;
        surface.gN *= -1;
        surface.sN *= -1;
    }

    return surface;
}