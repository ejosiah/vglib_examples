#version 460

/* TODO
 This need to be done in a compute shader. There is more than one triangle that will return
 a barycenter less than one, so we need to collected all of them and sort by their z depth
 and then pick the one with the smalled z depth
*/

#include "shared.glsl"
#include "quaternion.glsl"

layout(triangles) in;
layout(line_strip, max_vertices = 128) out;

layout(push_constant) uniform PushConstants {
    vec2 start;
    vec2 end;
    int state;
} pc;


vec3 computeBarycentricCoordiates(vec2 a, vec2 b, vec2 c, vec2 p) {
    vec2 v0 = b - a;
    vec2 v1 = c - a;
    vec2 v2 = p - a;

    float d00 = dot(v0, v0);
    float d01 = dot(v0, v1);
    float d11 = dot(v1, v1);
    float d20 = dot(v2, v0);
    float d21 = dot(v2, v1);

    float denom = d00 * d11 - d01 * d01;

    vec3 t;
    t.y = (d11 * d20 - d01 * d21) / denom;
    t.z = (d00 * d21 - d01 * d20) / denom;
    t.x = 1 - t.y - t.z;

    return t;
}

layout(location = 0) in vec4 wp[];

void main(){
    float delta = 2 * PI/127;

    if (wp[0].w == 1 && distance(pc.start, pc.end) > 0){
        vec2 p0 = globals.resolution * gl_in[0].gl_Position.xy / gl_in[0].gl_Position.w;
        vec2 p1 = globals.resolution * gl_in[1].gl_Position.xy / gl_in[1].gl_Position.w;
        vec2 p2 = globals.resolution * gl_in[2].gl_Position.xy / gl_in[2].gl_Position.w;

        vec3 t = computeBarycentricCoordiates(p0, p1, p2, pc.start);

        if (t.x + t.y + t.z < 1){
            vec3 e0 = wp[1].xyz - wp[0].xyz;
            vec3 e1 = wp[2].xyz - wp[0].xyz;

            vec3 n = cross(e0, e1);
            vec3 right = normalize(cross(e1, n));

            vec3 center = wp[0].xyz * t.x + wp[1].xyz * t.y + wp[2].xyz * t.z;

            t = computeBarycentricCoordiates(p0, p1, p2, pc.end);
            vec3 p = wp[0].xyz * t.x + wp[1].xyz * t.y + wp[2].xyz * t.z;

            float radius = distance(center, p);

            debugPrintfEXT("p: [%f, %f, %f], r: %f\n", wp[0].x, wp[0].y, wp[0].z, radius);

            p = center + right * radius;

            for (int i = 0; i < 128; i++) {
                float angle = delta * i;
                vec4 q = quatFromAxisAngle(n, angle);
                vec3 vertex = rotatePoint(q, p);
                gl_Position = globals.viewProjectionMatrix * vec4(vertex, 1);
                EmitVertex();
            }
        }
    }
}