#version 460 core

layout(set = 0, binding = 1) uniform sampler2DArray shadowMap;

layout(push_constant) uniform Contants {
    uint numCascades;
};

layout(location = 0) in vec2 fuv;

layout(location = 0) out vec4 fragColor;

void main(){

    float w = ceil(sqrt(numCascades));
    float h = numCascades/w;
    vec2 grid = vec2(w, h);
    vec2 gid = floor(fuv * grid);
    int id = int(gid.y * w + gid.x);
    vec2 uv = fract(fuv * grid);

    if(id >= numCascades) discard;

    fragColor = texture(shadowMap, vec3(uv, id)).rrrr;
}