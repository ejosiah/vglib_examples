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
};

bool test(vec3 o, vec3 rd, out vec3 pos);

bool outOfBounds(vec3 pos);

layout(location = 0) out vec4 fragColor;

void main() {
    gl_FragDepth = 1;

    vec3 o = (inverse(view) * vec4(0, 0, 0, 1)).xyz;
    vec3 rd = normalize(fs_in.direction);

    vec3 pos;

    if(test(o, rd, pos)) {
        vec3 tn = dFdx(pos);
        vec3 bn = dFdy(pos);
        vec3 n = normalize(cross(tn, bn));

        ivec3 voxelDim = textureSize(Voxels, 0);
        int maxSteps = max(voxelDim.x, max(voxelDim.y, voxelDim.z));
        float delta = 1/float(maxSteps);
        vec3 start = (worldToVoxelTransform * vec4(pos, 1)).xyz + rd * delta * 0.5;
        for(int i = 0; i < maxSteps; ++i) {
            pos = start + rd * delta * i;

            uint val = texture(Voxels, pos).r;
            if(val == 1) {


                pos = (voxelToWordTransform * vec4(pos, 1)).xyz;
                vec4 clipPos = proj * view * model * vec4(pos, 1);
                clipPos /= clipPos.w;

                gl_FragDepth = clipPos.z;

//                fragColor = vec4(1, 0, 0, 1);
                fragColor.rgb = pos;
                break;
            }
        }
    }
}

bool test(vec3 o, vec3 rd, out vec3 pos) {
    float tmin = 0;
    float tmax = 1e10;

    for(int i = 0; i < 3; ++i) {
        if(abs(rd[i]) < 1e-6){
            // ray is parallel to slab. No hit if origin not within slab
            if(o[i] < bmin[i] || o[i] > bmax[i]) return false;
        }else {
            float invRd = 1.0/rd[i];
            float t1 = (bmin[i] - o[i]) * invRd;
            float t2 = (bmax[i] - o[i]) * invRd;

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
    pos = o + rd * tmin;
    return true;
}

bool outOfBounds(vec3 pos) {
    bvec3 near = lessThan(pos, vec3(0));
    bvec3 far = greaterThanEqual(pos, vec3(1));

    return any(near) || any(far);
}