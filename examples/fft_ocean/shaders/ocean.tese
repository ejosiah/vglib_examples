#version 460

#define PI 3.1415926535897
#define TWO_PI 6.283185307179586476925286766559

#define HF_TEXTURE_ID 7

layout(quads, equal_spacing, ccw) in;

layout(set = 0, binding = 10) uniform sampler2D global_textures[];

layout(push_constant) uniform Constants {
    mat4 model;
    mat4 view;
    mat4 projection;
    float time;
};

layout(location = 0) in vec2 vUv[gl_MaxPatchVertices];

layout(location = 0) out vec2 uv;

vec2 hash22(vec2 p){
    vec3 p3 = fract(vec3(p.xyx) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx+33.33);
    return fract((p3.xx+p3.yz)*p3.zy);
}

void main(){
    mat4 MVP = projection * view * model;

    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

    vec4 p0 = gl_in[0].gl_Position;
    vec4 p1 = gl_in[1].gl_Position;
    vec4 p2 = gl_in[2].gl_Position;
    vec4 p3 = gl_in[3].gl_Position;

    vec4 p = mix(mix(p0, p1, u), mix(p3, p2, u), v);

    uv = mix(mix(vUv[0], vUv[1], u), mix(vUv[3], vUv[2], u), v);

    vec3 offset = texture(global_textures[HF_TEXTURE_ID], uv).rgb;
    p.xyz +=  offset * 100;

    gl_Position =  MVP * p;
}