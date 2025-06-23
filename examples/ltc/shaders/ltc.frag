#version 460 core

#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#include "ltc.glsl"

layout(set = 0, binding = 0) uniform sampler2D ltc_mat;
layout(set = 0, binding = 1) uniform sampler2D ltc_mag;

layout(set = 1, binding = 0, scalar) uniform Options {
    mat4  view;
    vec3 eyes;
    vec2 resolution;
    int   sampleCount;
    float roughness;
    vec3  dcolor;
    vec3  scolor;

    float intensity;
    float width;
    float height;
    float roty;
    float rotz;

    int twoSided;
};

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

// Tracing and intersection
///////////////////////////

struct Ray
{
    vec3 origin;
    vec3 dir;
};

struct Rect
{
    vec3  center;
    vec3  dirx;
    vec3  diry;
    float halfx;
    float halfy;

    vec4  plane;
};

bool RayPlaneIntersect(Ray ray, vec4 plane, out float t)
{
    t = -dot(plane, vec4(ray.origin, 1.0))/dot(plane.xyz, ray.dir);
    return t > 0.0;
}

bool RayRectIntersect(Ray ray, Rect rect, out float t)
{
    bool intersect = RayPlaneIntersect(ray, rect.plane, t);
    if (intersect)
    {
        vec3 pos  = ray.origin + ray.dir*t;
        vec3 lpos = pos - rect.center;

        float x = dot(lpos, rect.dirx);
        float y = dot(lpos, rect.diry);

        if (abs(x) > rect.halfx || abs(y) > rect.halfy)
        intersect = false;
    }

    return intersect;
}

// Camera functions
///////////////////

Ray GenerateCameraRay(float u1, float u2)
{
    Ray ray;

    // Random jitter within pixel for AA
    vec2 xy =  2.0*(gl_FragCoord.xy)/resolution - vec2(1.0);

    ray.dir = normalize(vec3(xy, 2.0));

    float focalDistance = 2.0;
    float ft = focalDistance/ray.dir.z;
    vec3 pFocus = ray.dir*ft;

    ray.origin = vec3(0);
    ray.dir    = normalize(pFocus - ray.origin);

    // Apply camera transform
    ray.origin = (view*vec4(ray.origin, 1)).xyz;
    ray.dir    = (view*vec4(ray.dir, 0)).xyz;

    return ray;
}

vec3 rotation_y(vec3 v, float a)
{
    vec3 r;
    r.x =  v.x*cos(a) + v.z*sin(a);
    r.y =  v.y;
    r.z = -v.x*sin(a) + v.z*cos(a);
    return r;
}

vec3 rotation_z(vec3 v, float a)
{
    vec3 r;
    r.x =  v.x*cos(a) - v.y*sin(a);
    r.y =  v.x*sin(a) + v.y*cos(a);
    r.z =  v.z;
    return r;
}

vec3 rotation_yz(vec3 v, float ay, float az)
{
    return rotation_z(rotation_y(v, ay), az);
}

// Scene helpers
////////////////

void InitRect(out Rect rect)
{
    rect.dirx = rotation_yz(vec3(1, 0, 0), roty*2.0*pi, rotz*2.0*pi);
    rect.diry = rotation_yz(vec3(0, 1, 0), roty*2.0*pi, rotz*2.0*pi);

    rect.center = vec3(0, 6, 32);
    rect.halfx  = 0.5*width;
    rect.halfy  = 0.5*height;

    vec3 rectNormal = cross(rect.dirx, rect.diry);
    rect.plane = vec4(rectNormal, -dot(rectNormal, rect.center));
}

void InitRectPoints(Rect rect, out vec3 points[4])
{
    vec3 ex = rect.halfx*rect.dirx;
    vec3 ey = rect.halfy*rect.diry;

    points[0] = rect.center - ex - ey;
    points[1] = rect.center + ex - ey;
    points[2] = rect.center + ex + ey;
    points[3] = rect.center - ex + ey;
}

// Misc. helpers
////////////////

float saturate(float v)
{
    return clamp(v, 0.0, 1.0);
}

vec3 PowVec3(vec3 v, float p)
{
    return vec3(pow(v.x, p), pow(v.y, p), pow(v.z, p));
}

const float gamma = 2.2;

vec3 ToLinear(vec3 v) { return PowVec3(v, gamma); }
vec3 ToSRGB(vec3 v)   { return PowVec3(v, 1.0/gamma); }

void main(){
    Rect rect;
    InitRect(rect);

    vec3 points[4];
    InitRectPoints(rect, points);

    vec4 floorPlane = vec4(0, 1, 0, 0);

    vec3 lcol = vec3(intensity);
    vec3 dcol = ToLinear(dcolor);
    vec3 scol = ToLinear(scolor);

    vec3 col = vec3(0);

    Ray ray = GenerateCameraRay(0.0, 0.0);

    float distToFloor;
    bool hitFloor = RayPlaneIntersect(ray, floorPlane, distToFloor);
    if (hitFloor)
    {
        vec3 pos = ray.origin + ray.dir*distToFloor;

        vec3 N = floorPlane.xyz;
        vec3 V = -ray.dir;

        vec2 uv = LTC_Coords(dot(N, V), roughness);
        mat3 Minv = LTC_Matrix(ltc_mat, uv);

        vec3 spec = LTC_Evaluate(N, V, pos, Minv, points, twoSided == 1);
        spec *= texture(ltc_mag, uv).r;

        vec3 diff = LTC_Evaluate(N, V, pos, mat3(1), points, twoSided == 1);

        col  = lcol*(scol*spec + dcol*diff);
        col /= 2.0*pi;
    }

    float distToRect;
    if (RayRectIntersect(ray, rect, distToRect))
    if ((distToRect < distToFloor) || !hitFloor)
    col = lcol;

    fragColor = vec4(col, 1.0);
}