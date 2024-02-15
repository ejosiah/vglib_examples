#version 460

layout(push_constant) uniform SearchArea {
  vec2 position;
  float radius;
};

layout(location = 0) out flat float vRadius;

void main() {
    vRadius = radius;
    gl_Position = vec4(position, 0, 1);
}