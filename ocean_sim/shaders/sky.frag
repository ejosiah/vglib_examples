#version 460

layout(set = 0, binding = 0) uniform sampler2D environmentMap;

layout(location = 0) in struct {
    vec3 pos;
} fs_in;

layout(location = 0) out vec4 fragColor;

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 sampleSphere(vec3 v){
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

void main(){
    vec2 uv = sampleSphere(normalize(fs_in.pos));
    vec3 envColor = texture(environmentMap, uv).rgb;
    fragColor = vec4(envColor, 1);
}