#version 460

layout(triangles_adjacency) in;
layout(line_strip, max_vertices = 6) out;

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
};


layout(set = 0, binding = 0) uniform UBO {
    vec3 lightDirection;
    vec3 cameraPosition;
};


layout(location = 0) in struct {
    vec4 color;
    vec3 position;
    vec3 normal;
    vec2 uv;
} gs_in[6];



void emitLine(int startIndex, int endIndex){
    gl_Position = gl_in[startIndex].gl_Position;
    EmitVertex();

    gl_Position = gl_in[endIndex].gl_Position;
    EmitVertex();
}

void main(){
    vec3 e1 = gs_in[2].position - gs_in[0].position;
    vec3 e2 = gs_in[4].position - gs_in[0].position;
    vec3 e3 = gs_in[1].position - gs_in[0].position;
    vec3 e4 = gs_in[3].position - gs_in[2].position;
    vec3 e5 = gs_in[4].position - gs_in[2].position;
    vec3 e6 = gs_in[5].position - gs_in[0].position;


    vec3 normal = cross(e1, e2);
    vec3 L = normalize(lightDirection);

    if(dot(normal, L) > 0.00001){
        normal = cross(e3, e1);

        if(dot(normal, L) <= 0){
            emitLine(0, 2);
        }

        normal = cross(e4, e5);
        if(dot(normal, L) <= 0){
            emitLine(2, 4);
        }

        normal = cross(e2, e6);
        if(dot(normal, L) <= 0){
            emitLine(4, 0);
        }

    }

}