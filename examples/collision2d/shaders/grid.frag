#version 460

struct Domain {
    vec2 lower;
    vec2 upper;
};

layout(push_constant) uniform PushCosntants {
    Domain domain;
    float spacing;
};

layout(location = 0) in struct {
    vec2 uv;
} fs_in;

layout(location = 0) out vec4 fragColor;

void main(){
    fragColor.rg = fs_in.uv;
    vec2 gPos = fs_in.uv * (domain.upper - domain.lower)/spacing;
    vec2 gid = floor(gPos);
    vec2 uv = fract(gPos);
    vec2 dd = step(0.99, uv);
    float d = max(dd.x, dd.y);
    fragColor = vec4(d, d, 0, 1);
}