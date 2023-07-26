#version 460

layout(triangles_adjacency) in;
layout(triangle_strip, max_vertices = 18) out;

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
} gs_in[6];


float EPSILON = 0.01;
mat4 mvp = proj * view * model;

// Emit a quad using a triangle strip
void EmitQuad(vec3 StartVertex, vec3 EndVertex)
{

    // Vertex #1: the starting vertex (just a tiny bit below the original edge)
    vec3 lightDir = normalize(StartVertex - lightPosition);
    gl_Position = mvp * vec4((StartVertex + lightDir * EPSILON), 1.0);
    EmitVertex();

    // Vertex #2: the starting vertex projected to infinity
    gl_Position = mvp * vec4(lightDir, 0.0);
    EmitVertex();

    // Vertex #3: the ending vertex (just a tiny bit below the original edge)
    lightDir = normalize(EndVertex - lightPosition);
    gl_Position = mvp * vec4((EndVertex + lightDir * EPSILON), 1.0);
    EmitVertex();

    // Vertex #4: the ending vertex projected to infinity
    gl_Position = mvp * vec4(lightDir , 0.0);
    EmitVertex();

    EndPrimitive();
}
void main(){
    vec3 e1 = gs_in[2].position - gs_in[0].position;
    vec3 e2 = gs_in[4].position - gs_in[0].position;
    vec3 e3 = gs_in[1].position - gs_in[0].position;
    vec3 e4 = gs_in[3].position - gs_in[2].position;
    vec3 e5 = gs_in[4].position - gs_in[2].position;
    vec3 e6 = gs_in[5].position - gs_in[0].position;


    vec3 normal = cross(e1, e2);
    vec3 lightDir = lightPosition - gs_in[0].position;

    // Handle only light facing triangles
    if(dot(normal, lightDir) > 0.00001){
        normal = cross(e3,e1);

        if (dot(normal, lightDir) <= 0) {
            vec3 StartVertex = gs_in[0].position;
            vec3 EndVertex = gs_in[2].position;
            EmitQuad(StartVertex, EndVertex);
        }

        normal = cross(e4,e5);
        lightDir = lightPosition - gs_in[2].position;

        if (dot(normal, lightDir) <= 0) {
            vec3 StartVertex = gs_in[2].position;
            vec3 EndVertex = gs_in[4].position;
            EmitQuad(StartVertex, EndVertex);
        }

        normal = cross(e2,e6);
        lightDir = lightPosition - gs_in[4].position;

        if (dot(normal, lightDir) <= 0) {
            vec3 StartVertex = gs_in[4].position;
            vec3 EndVertex = gs_in[0].position;
            EmitQuad(StartVertex, EndVertex);
        }

        // render the front cap
        lightDir = (normalize(gs_in[0].position - lightPosition));
        gl_Position = mvp * vec4((gs_in[0].position + lightDir * EPSILON), 1.0);
        EmitVertex();

        lightDir = (normalize(gs_in[2].position - lightPosition));
        gl_Position = mvp * vec4((gs_in[2].position + lightDir * EPSILON), 1.0);
        EmitVertex();

        lightDir = (normalize(gs_in[4].position - lightPosition));
        gl_Position = mvp * vec4((gs_in[4].position + lightDir * EPSILON), 1.0);
        EmitVertex();
        EndPrimitive();

        // render the back cap
        lightDir = gs_in[0].position - lightPosition;
        gl_Position = mvp * vec4(lightDir, 0.0);
        EmitVertex();

        lightDir = gs_in[4].position - lightPosition;
        gl_Position = mvp * vec4(lightDir, 0.0);
        EmitVertex();

        lightDir = gs_in[2].position - lightPosition;
        gl_Position = mvp * vec4(lightDir, 0.0);
        EmitVertex();

    }

}