#ifndef IMPLICITS_GLSL
#define IMPLICITS_GLSL

#include "constants.glsl"

const uint IMPLICIT_TYPE_PLANE = 1;
const uint IMPLICIT_TYPE_SPHERE = 2;
const uint IMPLICIT_TYPE_CYLINDER = 3;
const uint IMPLICIT_TYPE_BOX = 4;

struct Plane{
    vec3 normal;
    float d;
};

struct Sphere{
    vec3 center;
    float radius;
};

struct Cylinder{
    vec3 bottom;
    vec3 top;
    float radius;
};

struct Ray{
    vec3 origin;
    vec3 direction;
};

struct Box {
    vec3 min;
    vec3 max;
};

//bool box_ray_test(Box box, Ray ray, out vec2 t) {
//    float tmin = -FLT_MAX;
//    float tmax = FLT_MAX;
//    const vec3 o = ray.origin;
//    const vec3 d = ray.direction;
//    const Box b = box;
//
//    for(int i = 0; i < 3; ++i) {
//        if(abs(d[i]) < EPSILON) {
//            if(o[i] < b.min[i] || o[i] > b.max[i]) return false;
//        } else {
//            float ood = 1/d[i];
//            float t1 = (b.min[i] - o[i]) * ood;
//            float t2 = (b.max[i] - o[i]) * ood;
//
//            if (t1 > t2) {
//                float tmp = t1;
//                t1 = t2;
//                t2 = tmp;
//            }
//
//            tmin = max(tmin, t1);
//            tmax = min(tmax, t2);
//
//            if(tmin > tmax) return false;
//        }
//    }
//    t.x = max(0, tmin);
//    t.y = tmax;
//
//    return true;
//}

bool box_ray_test(Box box, Ray ray, out vec2 t) {
    float tmin = -1e10;
    float tmax = 1e10;

    vec3 o = ray.origin;
    vec3 rd = ray.direction;
    vec3 bmin = box.min;
    vec3 bmax = box.max;

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
    t.x = max(0, tmin);
    t.y = tmax;
    return true;
}

bool plane_ray_test(Plane p, Ray r, out float t){

    t = p.d - dot(p.normal, r.origin);
    t /= dot(p.normal, r.direction);

    return t > 0;
}

bool sphere_ray_test(Sphere s, Ray r, out vec2 t){
    vec3 m = r.origin - s.center;
    float a = dot(r.direction, r.direction);
    float b = dot(m, r.direction);
    float c = dot(m, m) - s.radius * s.radius;

    if(c > 0 && b > 0) return false; // ray origin is outside and facing away from sphere

    float discr = b * b - c * a;
    if(discr < 0) return false; // no real roots

    float t0 = (-b - sqrt(discr))/a;
    float t1 = (-b + sqrt(discr))/a;

    if(t1 < t0) {
        float tmp = t0;
        t0 = t1;
        t1 = tmp;
    }
    t0 = max(0, t0);

    t.x = t0; t.y = t1;

    return true;
}

bool cylinder_ray_test(Cylinder cylinder, Ray r, out float t){
    vec3 d = cylinder.top - cylinder.bottom;
    vec3 m = r.origin - cylinder.bottom;
    vec3 n = d;

    float md = dot(m, d);
    float nd = dot(n, d);
    float dd = dot(d, d);

    // Test if segment fully outside either endcap of cylinder
    if(md < 0 && md + nd < 0) return false; // segment outside of bottom side of cylinder
    if(md > dd && md + nd > dd) return false; // segment outside of top side of cylinder

    float nn = dot(n, n);
    float mn = dot(m, n);
    float a = dd * nn - nd * nd;
    float k = dot(m, m) - cylinder.radius * cylinder.radius;
    float c = dd * k - md * md;

    if(abs(a) < EPSILON){
        // segment runs paralle to cylinder axis
        if(c > 0) return false;
        // Now known that segment intersects cylinder; figure out how it intersects
        if(md < 0) t = -mn/nn;  // intersects segment agianst 'bottom' endcap
        else if (md > dd) t = (nd - mn) /nn; // intersect segment against 'top' endcap
        else t = 0; // 'a' lies inside cylinder;
        return true;
    }
    float b = dd * mn - nd * md;
    float discr = b * b - a * c;
    if(discr < 0) return false; // no real roots; no intersection

    t = (-b - sqrt(discr)) / a;
    if(t <  0) return false;
    if(md + t * nd < 0){
        // intersection outside cylinder on 'bottom' side
        if(nd <= 0) return false; // segment pointing away from endcap
        t = -md / nd;
        // keep intersection if dot(ray(t) - bottom, ray(t) - bottom) <= radius^2
        return k + 2 * t * (mn + t * nn) <= 0;
    }else if (md + t * nd > dd){
        // intersection outside cylinder on 'top' side
        if(nd >= 0) return false;   // segment pointing away from endcap
        t = (dd - md)/nd;
        // keep intersection if dot(ray(t) - top, ray(t) - top) <= radius^2
        return k + dd - 2 * md + t * (2 * (mn - nd) + t * nn) <= 0.0f;
    }
    return true;
}

float remap(float value, float oldMin, float oldMax, float newMin, float newMax){
    return (((value - oldMin) / (oldMax - oldMin)) * (newMax - newMin)) + newMin;
}

vec3 remap(vec3 value, vec3 oldMin, vec3 oldMax, vec3 newMin, vec3 newMax){
    return (((value - oldMin) / (oldMax - oldMin)) * (newMax - newMin)) + newMin;
}

vec2 remap(vec2 value, vec2 oldMin, vec2 oldMax, vec2 newMin, vec2 newMax){
    return (((value - oldMin) / (oldMax - oldMin)) * (newMax - newMin)) + newMin;
}

vec2 getUV(Sphere s, Ray r, float t){
    vec3 x = r.origin + r.direction * t;
    vec3 p = normalize(x - s.center);
    float phi = atan(p.z, p.x);
    float theta = asin(p.y);
    vec2 uv;
    uv.x = 1 - (phi + PI) / (2 * PI);
    uv.y = (theta + PI/2) / PI;

    return uv;
}

void getTangents(Sphere s, Ray r, float t, out vec3 tangent, out vec3 bitangent);

//vec2 getUV(Plane p, Ray r, float t);
//
//vec2 getUV(Cylinder cylinder, Ray r, float t);

#endif // IMPLICITS_GLSL