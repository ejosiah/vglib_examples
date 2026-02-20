#version 460 core

layout(location = 0) in vec4 iPosition;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec4 color;
layout(location = 5) in vec2 uv;

layout(push_constant) uniform Constants {
    mat4 model;
    mat4 viewProjection;
    vec4 probe_spacing;
    ivec4 probe_count;
};

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUv;

vec3 probe_index_to_grid_indices( int probe_index ) {
    const int probe_x = probe_index % probe_count.x;
    const int probe_counts_xy = probe_count.x * probe_count.y;

    const int probe_y = (probe_index % probe_counts_xy) / probe_count.x;
    const int probe_z = probe_index / probe_counts_xy;

    return vec3( probe_x, probe_y, probe_z ) * probe_spacing.xyz;
}

void main(){
    vColor = color;
    vUv = uv;

    vec3 gridIndices = probe_index_to_grid_indices(gl_InstanceIndex);
    vec3 position = (model * iPosition).xyz  + gridIndices;

    gl_Position = viewProjection * vec4(position, 1);
}