#version 450

#include "terrain_ubo.glsl"
#include "frustum_cull.glsl"

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

struct Vertex{
    vec4 position;
    vec4 color;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec2 uv;
};

layout(set = 2, binding = 0) buffer TRI_COUNT {
    int count;
};

layout(set = 2, binding = 1) buffer TRI_VERTCIES {
    Vertex vertices[];
};

layout(location = 0) in vec3 worldPosition_in[3];
layout(location = 1) in vec3 normal_in[3];
layout(location = 2) in vec2 uv_in[3];
layout(location = 3) in vec2 patch_uv_in[3];
layout(location = 4) in vec3 color_in[3];

layout(location = 0) out struct {
   vec3 worldPosition;
   vec3 normal;
   vec2 uv;
   vec2 patch_uv;
   vec3 color;
} gs_out;

layout(location = 5) noperspective out vec3 edgeDist;

bool triangleLineTest(vec3 a, vec3 b, vec3 c, vec3 p, vec3 q, out float t, out float u, out float v, out float w);

void resolveCollision(vec3 a, vec3 b, vec3 c){

    float penetrationDepth = dot(vec3(0, 1, 0), cameraPosition);
    if(penetrationDepth < 0){
        vec3 p = cameraPosition - velocity * abs(penetrationDepth);
        vec3 q = cameraPosition;

        float t, u, v, w;
        if (triangleLineTest(a, b, c, p, q, t, u, v, w)){
            collisionPoint = a * u + b * v + c * w;
            collision = 1;
        }
    }
}

vec3 edgeDistance(vec3 p0, vec3 p1, vec3 p2){
    float a = distance(p1, p2);
    float b = distance(p0, p1);
    float c = distance(p0, p2);

    float alpha = acos((b * b + c * c - a * a)/(2 * b * c));
    float beta =  acos((a * a + c * c - b * b)/(2 * a * c));

    float ha = abs(c * sin(beta));
    float hb = abs(c * sin(alpha));
    float hc = abs(b * sin(alpha));

    return vec3(ha, hb, hc);
}

bool cullTriangle(vec3 p0, vec3 p1, vec3 p2){
    vec3 bMin = vec3(3.4028235e+38);
    vec3 bMax = vec3(1.1754944e-38);

    bMin = min(bMin, p0);
    bMin = min(bMin, p1);
    bMin = min(bMin, p1);

    bMax = max(bMax, p0);
    bMax = max(bMax, p1);
    bMax = max(bMax, p2);

    return !isBoxInFrustum(projection * view, bMin, bMax);
}

void main(){
    int triangle = atomicAdd(count, 1);

    vec3 p0 = gl_in[0].gl_Position.xyz;
    vec3 p1 = gl_in[1].gl_Position.xyz;
    vec3 p2 = gl_in[2].gl_Position.xyz;

    resolveCollision(p0, p1, p2);

    Vertex v0, v1, v2;
    v0.position = vec4(p0, 1);
    v1.position = vec4(p1, 1);
    v2.position = vec4(p2, 1);

    v0.normal = normal_in[0];
    v1.normal = normal_in[1];
    v2.normal = normal_in[2];

    v0.uv = uv_in[0];
    v1.uv = uv_in[0];
    v2.uv = uv_in[0];

    v0.color = vec4(1);
    v1.color = vec4(1);
    v2.color = vec4(1);

    vertices[triangle * 3 + 0] = v0;
    vertices[triangle * 3 + 1] = v1;
    vertices[triangle * 3 + 2] = v2;

    vec3 edgeDisComb = edgeDistance(p0, p1, p2);

    for(int i = 0; i < gl_in.length(); i++){
        gs_out.worldPosition = worldPosition_in[i];
        gs_out.normal = normal_in[i];
        gs_out.uv = uv_in[i];
        gs_out.patch_uv = patch_uv_in[i];
        gs_out.color = color_in[i];

        edgeDist = vec3(0);
        edgeDist[i] = edgeDisComb[i];

        gl_Position = MVP * gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}

bool triangleLineTest(vec3 a, vec3 b, vec3 c, vec3 p, vec3 q, out float t, out float u, out float v, out float w){
    vec3 ab = b - a;
    vec3 ac = c - a;
    vec3 qp = p - q;

    vec3 n = cross(ab, ac);

    float d = dot(qp, n);
    if(d <= 0) return false;

    vec3 ap = p - a;
    t = dot(ap, n);
    if(t < 0) return false;
    if(t > d) return false;

    vec3 e = cross(qp, ap);
    v = dot(ac, e);
    if(v < 0 || v > d) return false;
    w = -dot(ab, e);
    if(w < 0 || v + w > d) return false;

    float ood = 1/d;
    t *= ood;
    v *= ood;
    w *= ood;
    u = 1 - v - w;

    return true;

}