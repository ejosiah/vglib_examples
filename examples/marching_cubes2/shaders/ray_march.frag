#version 460

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 bmin;
    vec3 bmax;
};

layout(location = 0) in struct {
    vec3 direction;
} fs_in;

layout(set = 0, binding = 0) uniform sampler3D Voxels;

layout(set = 0, binding = 1) buffer Params {
    mat4 worldToVoxelTransform;
    mat4 voxelToWordTransform;
    int numVoxels;
};

bool test(vec3 o, vec3 rd, out vec3 pos);

bool outOfBounds(vec3 pos);

vec3 bisection(vec3 left, vec3 right, float iso);

vec3 computeNormal(vec3 p0, vec3 p1, float isoValue);

layout(location = 0) out vec4 fragColor;

void main() {
    gl_FragDepth = 1;

    vec3 o = (inverse(view) * vec4(0, 0, 0, 1)).xyz;
    vec3 rd = normalize(fs_in.direction);

    vec3 pos;

    const float isoValue = 1e-6;

    if(test(o, rd, pos)) {
        ivec3 voxelDim = textureSize(Voxels, 0);
        int maxSteps = max(voxelDim.x, max(voxelDim.y, voxelDim.z));
        float delta = 1/float(maxSteps);

        vec3 start = (worldToVoxelTransform * vec4(pos, 1)).xyz + sign(rd) * 0.5/vec3(voxelDim);
        vec3 pos = (worldToVoxelTransform * vec4(pos + rd, 1)).xyz + sign(rd) * 0.5/vec3(voxelDim);
        rd = normalize(pos - start) * delta;
        for(int i = 0; i < maxSteps; ++i) {
            vec3 pos0 = start + rd * i;

            if(outOfBounds(pos0)) break;

            vec3 pos1 = pos + rd;
            float val = texture(Voxels, pos0).r;
            float val1 = texture(Voxels, pos0 + rd).r;

            if((val - isoValue) < 0 && (val1 - isoValue) >= 0) {
                pos = (voxelToWordTransform * vec4(pos0, 1)).xyz;
                vec4 clipPos = proj * view * model * vec4(pos, 1);
                clipPos /= clipPos.w;

                vec3 N = computeNormal(pos0, pos1, isoValue);
                vec3 L = normalize(vec3(1));

                fragColor.rgb = vec3(0, 1, 1) * max(0, dot(N, L));
                gl_FragDepth = clipPos.z;
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

float F(vec3 x) {
    return texture(Voxels, x).r;
}

vec3 bisection(vec3 left, vec3 right, float iso) {
    for(int i = 0; i < 4; ++i) {
        vec3 midpoint = (right + left) * 0.5;
        float cM = texture(Voxels, midpoint).x;
        if(cM < iso){
            left = midpoint;
        }else {
            right = midpoint;
        }
    }
    return (right + left) * 0.5;
}

vec3 computeNormal(vec3 p0, vec3 p1, float isoValue) {

    vec3 p = bisection(p0, p1, isoValue);

    vec3 d = 1/vec3(textureSize(Voxels, 0));
    float dx = (F(vec3(p.x + d.x, p.yz)) - F(vec3(p.x - d.x, p.yz))) * 0.5 ;
    float dy = (F(vec3(p.x, p.y + d.y, p.z)) - F(vec3(p.x, p.y - d.y, d.z))) * 0.5 ;
    float dz = (F(vec3(p.xy, p.z + d.z)) - F(vec3(p.xy, p.z - d.z))) * 0.5;

    return normalize(vec3(dx, dy, dz));
}