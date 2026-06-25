#version 460

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 bmin;
    vec4 bmax;
};

layout(location = 0) in struct {
    vec3 direction;
} fs_in;

layout(set = 0, binding = 3) uniform usampler3D Voxels;

layout(set = 0, binding = 2) buffer Params {
    mat4 worldToVoxelTransform;
    mat4 voxelToWordTransform;
    int numVoxels;
    int maxVoxels;
};

bool testUnitCube(vec3 o, vec3 rd, out float tmin, out float tmax);

bool outOfBounds(vec3 pos);

layout(location = 0) out vec4 fragColor;

void main() {
    gl_FragDepth = 1;

    vec3 worldOrigin = (inverse(view) * vec4(0, 0, 0, 1)).xyz;
    vec3 worldDirection = normalize(fs_in.direction);

    vec3 rayOrigin = (worldToVoxelTransform * vec4(worldOrigin, 1)).xyz;
    vec3 rayDirection = normalize((worldToVoxelTransform * vec4(worldDirection, 0)).xyz);

    float tEnter;
    float tExit;

    if(testUnitCube(rayOrigin, rayDirection, tEnter, tExit)) {
        ivec3 voxelDim = textureSize(Voxels, 0);
        int maxDim = max(voxelDim.x, max(voxelDim.y, voxelDim.z));
        float delta = 1.0 / float(maxDim);
        float t = max(tEnter, 0.0) + delta * 0.5;
        int maxSteps = max(1, int(ceil((tExit - t) / delta)) + 1);

        for(int i = 0; i < maxSteps; ++i, t += delta) {
            vec3 pos = rayOrigin + rayDirection * t;

            if(outOfBounds(pos)) break;

            uint val = texture(Voxels, pos).r;
            if(val == 1) {

                vec3 worldPos = (voxelToWordTransform * vec4(pos, 1)).xyz;
                vec4 clipPos = proj * view * model * vec4(worldPos, 1);
                clipPos /= clipPos.w;

                gl_FragDepth = clipPos.z;

//                fragColor = vec4(1, 0, 0, 1);
                fragColor.rgb = vec3(1, 0, 0);
                break;
            }
        }
    }
}

bool testUnitCube(vec3 o, vec3 rd, out float tmin, out float tmax) {
    tmin = 0;
    tmax = 1e10;

    for(int i = 0; i < 3; ++i) {
        if(abs(rd[i]) < 1e-6){
            // ray is parallel to slab. No hit if origin not within slab
            if(o[i] < 0 || o[i] > 1) return false;
        }else {
            float invRd = 1.0/rd[i];
            float t1 = (0 - o[i]) * invRd;
            float t2 = (1 - o[i]) * invRd;

            if(t1 > t2) {
                float temp = t1;
                t1 = t2;
                t2 = temp;
            }
            tmin = max(tmin, t1);
            tmax = min(tmax, t2);
            if(tmin > tmax) return false;
        }
    }
    return tmax >= max(tmin, 0.0);
}

bool outOfBounds(vec3 pos) {
    bvec3 near = lessThan(pos, vec3(0));
    bvec3 far = greaterThanEqual(pos, vec3(1));

    return any(near) || any(far);
}
