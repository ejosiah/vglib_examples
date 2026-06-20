#version 460

layout(push_constant) uniform Constants {
    mat4 transform;
    vec4 color;
    vec2 position;
    vec2 velocity;
    vec2 domainMin;
    vec2 domainMax;
    float radius;
    float size;
    uint scene;
};

layout(location = 0) out vec2 uv;

void main() {
    uv = vec2((gl_VertexIndex >> 1) & 1, gl_VertexIndex & 1);
    vec2 domainPosition = mix(domainMin, domainMax, uv);
    vec4 position = vec4(domainPosition, 0.5f, 1.0f);
    gl_Position = transform * position;
}
