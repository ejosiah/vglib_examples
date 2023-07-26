#version 460

layout(triangles) in;
layout(triangle_strip, max_vertices = 6) out;

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
};

layout(set = 0, binding = 0) uniform UBO {
    vec3 lightPosition;
    vec3 cameraPosition;
};


layout(location = 0) in struct {
    vec3 position;
} gs_in[3];


float EPSILON = 0.01;
mat4 mvp = proj * view * model;

void main(){
    // Calculate the normal vector for the triangle in view space
    vec3 normal = normalize(cross(gs_in[1].position.xyz - gs_in[0].position.xyz, gs_in[2].position.xyz - gs_in[0].position.xyz));

    // Determine if the triangle is facing towards or away from the light
    bool towardsLight = dot(normal, lightPosition - gs_in[0].position.xyz) > 0;

    // Iterate over each vertex in the triangle
    for (int i = 0; i < 3; i++) {
        vec3 v = gs_in[i].position.xyz;
        vec3 l = lightPosition;

        // If the triangle is facing towards the light, don't include it in the shadow volume
        if (towardsLight) {
            continue;
        }

        // Calculate the position of the end cap vertex on the light side of the shadow volume
        vec3 lv = l + (v - l) * 100000.0;

        // Output the triangle vertex and the end cap vertex
        gl_Position = mvp * vec4(v, 1.0);
        EmitVertex();
        gl_Position = mvp * vec4(lv, 1.0);
        EmitVertex();
    }
    EndPrimitive();
}