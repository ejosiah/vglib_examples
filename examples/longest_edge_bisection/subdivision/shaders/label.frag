#version 460

#extension GL_EXT_debug_printf : enable

#define CBT_HEAP_BUFFER_BINDING 0
#include "cbt.glsl"
#include "leb.glsl"

layout(set = 1, binding = 0) uniform sampler2D atlas;

layout(location = 0) flat in int oHeapIndex;
layout(location = 0) out vec4 fragColor;


void main() {
    cbt_Node node = cbt_DecodeNode(0, oHeapIndex);

    vec2 size = textureSize(atlas, 0);
    vec2 uv = vec2(node.id % 32, node.id / 32);
    uv = (uv + gl_PointCoord.xy) * 32/size;

    vec4 color = texture(atlas, uv);

    if(color.a < 0.5) discard;

    fragColor = vec4(color.rgb, 1);
}