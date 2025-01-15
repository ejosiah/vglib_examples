#ifndef MODELS_GLSL
#define MODELS_GLSL

#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#define KEY_ADD 0
#define KEY_REMOVE 1
#define KEY_CHECK 2
#define POOL_SIZE 300

#define DISPATCH_GEN_3D_TEXTURE 0
#define GID gl_GlobalInvocationID
#define SIZE (gl_NumWorkGroups * gl_WorkGroupSize)
#define GI_INDEX ((GID.z * SIZE.y + GID.y) * SIZE.x + GID.x)

struct DebugData {
    vec3 my_block;
    vec3 their_block;
    float my_distance;
    float their_distance;
    int eviction_id;
    int evicted;
    int skipped;
    int vertex_count;
    int slot;
};

struct DrawCommand {
    uint  vertexCount;
    uint  instanceCount;
    uint  firstVertex;
    uint  firstInstance;
    vec4 aabb;
    uint vertex_id;
};

struct DispatchCommand {
    uint x;
    uint y;
    uint z;
};

#define DEFINE_REMAP(Type) \
 Type remap(Type x, Type a, Type b, Type c, Type d) { \
    return mix(c, d, (x - a)/(b - a)); \
 }

DEFINE_REMAP(float)
DEFINE_REMAP(vec2)
DEFINE_REMAP(vec3)
DEFINE_REMAP(vec4)

#define BLOCK_STATE_OUTSIDE 0U
#define BLOCK_STATE_INSIDE 1U
#define BLOCK_STATE_EVICTED 2U

struct BlockData {
    vec3 aabb;
    uint voxel_id;
};

struct Vertex {
    vec3 position;
    vec3 normal;
    float ambient_occulsion;
};

layout(constant_id = 0) const float BLOCK_SIZE = 1;
float HALF_BLOCK_SIZE = BLOCK_SIZE * 0.5;

layout(set = 0, binding = 0, scalar) buffer CameraInfoUbo {
    mat4 view_projection;
    mat4 inverse_view_projection;
    mat4 grid_to_world;
    vec4 frustum[6];
    vec3 position;
    vec3 aabbMin;
    vec3 aabbMax;
    vec3 direction;
    float offset;
} camera_info;

layout(set = 1, binding = 0, scalar) buffer vertexDataBuffer {
    Vertex data[];
} vertex[];

layout(set = 1, binding = 1, scalar) buffer ssboBlockData3 {
    BlockData block[];
};

layout(set = 1, binding = 2, std430) buffer distanceToCameraBuffer {
    uint data[];
} distance_to_camera[2];

layout(set = 1, binding = 3) buffer AtomicsBuffer {
    int free_slots;
    int eviction_id;
    uint processed_block_add_id;
    uint empty_block_add_id;
    uint blocks;
    int slots_used;
    uint debug_id;
} counters;

layout(set = 1, binding = 4, scalar) buffer BlockHashSsbo {
    uint processed_block_keys[];
};

layout(set = 1, binding = 5, scalar) buffer Empty_BlockHashSsbo {
    uint empty_block_keys[];
};

layout(set = 1, binding = 6, r32f) uniform image3D voxels[];

layout(set = 1, binding = 11, scalar) buffer DebugSsbo {
    DebugData debug[];
};

layout(set = 1, binding = 12) uniform sampler3D noise[3];

layout(set = 2, binding = 0, scalar) buffer DispatchIndirectSsbo {
    DispatchCommand dispatch[];
};

layout(set = 2, binding = 1, scalar) buffer DrawIndirectSsbo {
    DrawCommand draw[];
};

float cb = HALF_BLOCK_SIZE;

vec4 corners[8] = vec4[8](
    vec4( -cb, -cb, -cb, 0.5 ), vec4(cb, -cb, -cb, 0.5 ),
    vec4(cb, -cb, cb, 0.5 ), vec4(-cb, -cb, cb, 0.5 ),
    vec4( -cb, cb, -cb, 0.5 ), vec4(cb, cb, -cb, 0.5 ),
    vec4(cb, cb, cb, 0.5 ), vec4(-cb, cb, cb, 0.5 )
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
    vec3 dim = vec3(128, 32, 256);
//    p = mod(p, vec3(1000)); // I'm assuming grid volume is 1km^3
//   return uint(p.z * 163 + p.y * 397 + p.x * 509);
    p = mod(p/BLOCK_SIZE, dim);
    dim *= 0.5;
    return uint((p.z * dim.y + p.y) * dim.x + p.x);
}

float computeDistanceToCamera(vec3 position) {
    const vec3 camera_direction = camera_info.direction;
    const vec3 camera_position = camera_info.position;
    vec3 cam_to_position = position - camera_position;

    return sign(dot(camera_direction, cam_to_position)) * distance(camera_position, position);

}

#endif // MODELS_GLSL