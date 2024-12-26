#ifndef MODELS_GLSL
#define MODELS_GLSL

#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#define KEY_ADD 0
#define KEY_REMOVE 1
#define KEY_CHECK 2
#define POOL_SIZE 300


#define DEFINE_REMAP(Type) \
 Type remap(Type x, Type a, Type b, Type c, Type d) { \
    return mix(c, d, (x - a)/(b - a)); \
 }

DEFINE_REMAP(float)
DEFINE_REMAP(vec2)
DEFINE_REMAP(vec3)
DEFINE_REMAP(vec4)

struct BlockData {
    vec3 aabb;
    uint vertex_id;
    uint voxel_id;
    uint vertex_count;
    float distanceToCam;
};

layout(set = 0, binding = 0, scalar) buffer CameraInfoUbo {
    mat4 view_projection;
    mat4 inverse_view_projection;
    mat4 grid_to_world;
    vec4 frustum[6];
    vec3 position;
    vec3 aabbMin;
    vec3 aabbMax;
    vec3 direction;
} camera_info;

layout(set = 1, binding = 0, scalar) buffer vertexDataBuffer {
    vec3 position;
    vec2 normal;
    float ambient_occulsion;
} vertex[];

layout(set = 1, binding = 1, scalar) buffer ssboBlockData3 {
    BlockData block[];
};

layout(set = 1, binding = 2, std430) buffer distanceToCameraBuffer {
    uint distance_to_camera[];
};

layout(set = 1, binding = 3) buffer AtomicsBuffer {
    uint block_id;
    uint set_add_id;
} counters;

layout(set = 1, binding = 4, scalar) buffer BlockHashSsbo {
    uint block_keys[];
};

layout(set = 1, binding = 5, r32f) uniform writeonly image3D voxels[];


const vec4 corners[8] = vec4[8](
    vec4( -0.5, -0.5, -0.5, 0.5 ), vec4(0.5, -0.5, -0.5, 0.5 ),
    vec4(0.5, -0.5, 0.5, 0.5 ), vec4(-0.5, -0.5, 0.5, 0.5 ),
    vec4( -0.5, 0.5, -0.5, 0.5 ), vec4(0.5, 0.5, -0.5, 0.5 ),
    vec4(0.5, 0.5, 0.5, 0.5 ), vec4(-0.5, 0.5, 0.5, 0.5 )
);

bool box_in_frustum_test(vec4 frustum[6], vec3 center) {
    const vec4 bc = vec4(center, 0.5);
    for(int i = 0; i < 6; ++i) {
        float outside = 0;
        outside += step(dot( frustum[i], bc + corners[0]), 0);
        outside += step(dot( frustum[i], bc + corners[1]), 0);
        outside += step(dot( frustum[i], bc + corners[2]), 0);
        outside += step(dot( frustum[i], bc + corners[3]), 0);
        outside += step(dot( frustum[i], bc + corners[4]), 0);
        outside += step(dot( frustum[i], bc + corners[5]), 0);
        outside += step(dot( frustum[i], bc + corners[6]), 0);
        outside += step(dot( frustum[i], bc + corners[7]), 0);

        if (outside == 8) return false;
    }

    return true;
}

uint compute_hash_key(vec3 p) {
   return uint(p.z * 31 + p.y * 79 + p.x * 541);
}

#endif // MODELS_GLSL