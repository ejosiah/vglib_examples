#ifndef VOLUME_RENDERING_COMMONG_GLSL
#define VOLUME_RENDERING_COMMONG_GLSL

struct VolumeInfo {
    mat4 worldToVoxelTransform;
    mat4 voxelToWordTransform;
    vec3 bmin;
    vec3 bmax;
};

layout(set = 0, binding = 0) uniform sampler3D density;
layout(set = 0, binding = 1) uniform sampler3D emission;

layout(set = 1, binding = 0, scalar) buffer Info {
    VolumeInfo info;
};

bool test(vec3 o, vec3 rd, out vec3 pos, vec3 bmin, vec3 bmax) {
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
    return texture(density, x).r;
}

vec3 bisection(vec3 left, vec3 right, float iso) {
    for(int i = 0; i < 4; ++i) {
        vec3 midpoint = (right + left) * 0.5;
        float cM = texture(density, midpoint).x;
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

    vec3 d = 1/vec3(textureSize(density, 0));
    float dx = (F(vec3(p.x + d.x, p.yz)) - F(vec3(p.x - d.x, p.yz))) * 0.5 ;
    float dy = (F(vec3(p.x, p.y + d.y, p.z)) - F(vec3(p.x, p.y - d.y, d.z))) * 0.5 ;
    float dz = (F(vec3(p.xy, p.z + d.z)) - F(vec3(p.xy, p.z - d.z))) * 0.5;

    return normalize(vec3(dx, dy, dz));
}



#endif // VOLUME_RENDERING_COMMONG_GLSL