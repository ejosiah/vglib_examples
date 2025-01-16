#version 460

layout(push_constant) uniform Cosntants {
    vec2 target;
};

layout(location = 0) out vec2 uv;

void main(){
    gl_PointSize = 11;
    vec2 position = (2 * target - 1);
    position.y *= -1;
    gl_Position = vec4(position, 0, 1);
}