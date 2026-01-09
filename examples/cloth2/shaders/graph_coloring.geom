#version 460

layout(points) in;
layout(line_strip, max_vertices = 2) out;

layout(set = 0, binding = 0) buffer POINT_MASSES_IN {
    vec4 positions[];
};

layout(set = 1, binding = 0) buffer CONSTRAINT_IDS {
    int constraintIDs[];
};

layout(set = 1, binding = 1) buffer REST_LENGTHS {
    float restLengths[];
};

layout(push_constant) uniform Cosntants {
    mat4 model;
    mat4 view;
    mat4 projection;
    int pass;
    int offset;
};

layout(location = 0) flat in int iCID[];

layout(location = 0) out vec4 color;
layout(location = 1) out vec2 uv;

vec4 colors[5] = vec4[5](
    vec4(0.90, 0.20, 0.20, 1.0), // Pass 0 – Red (vertical even)
    vec4(0.20, 0.60, 0.90, 1.0), // Pass 1 – Blue (vertical odd)
    vec4(0.20, 0.85, 0.30, 1.0), // Pass 2 – Green (horizontal even)
    vec4(0.95, 0.85, 0.20, 1.0), // Pass 3 – Yellow (horizontal odd)
    vec4(0.75, 0.30, 0.85, 1.0)  // Pass 4 – Purple (shear / bending / conflicts)
);

void main(){
    const int cid = iCID[0] + offset;
    mat4 mvp = projection * view * model;

    vec3 pOffset = vec3(0, pass == 4 ? 0.25 : 0, 0);

    vec3 p = positions[constraintIDs[2 * cid]].xyz + pOffset;
    color = colors[pass];
    gl_Position = mvp * vec4(p, 1);
    EmitVertex();

    p = positions[constraintIDs[2 * cid + 1]].xyz + pOffset;
    color = colors[pass];
    gl_Position = mvp * vec4(p, 1);
    EmitVertex();

    EndPrimitive();
}