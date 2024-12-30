#ifndef CUBE_GLSL
#define CUBE_GLSL


// FRONT FACE
vec3 cubeVertices[24] = vec3[24](
    vec3(-0.5, -0.5, 0.5),
    vec3(0.5,  -0.5, 0.5),
    vec3(0.5,  0.5,  0.5),
    vec3(-0.5, 0.5,  0.5),
    
    // RIGHT FACE
    vec3(0.5,  -0.5, 0.5), 
    vec3(0.5,  -0.5, -0.5),
    vec3(0.5,  0.5,  -0.5),
    vec3(0.5,  0.5,  0.5),
    
    // BACK FACE
    vec3(-0.5, -0.5, -0.5),
    vec3(-0.5, 0.5,  -0.5),
    vec3(0.5,  0.5,  -0.5),
    vec3(0.5,  -0.5, -0.5),
    
    // LEFT FACE
    vec3(-0.5, -0.5, 0.5),
    vec3(-0.5, 0.5,  0.5), 
    vec3(-0.5, 0.5,  -0.5),
    vec3(-0.5, -0.5, -0.5),
    
    // BOTTOM FACE
    vec3(-0.5, -0.5, 0.5), 
    vec3(-0.5, -0.5, -0.5), 
    vec3(0.5,  -0.5, -0.5), 
    vec3(0.5,  -0.5, 0.5),  
    
    // TOP FACE
    vec3(-0.5, 0.5,  0.5), 
    vec3(0.5,  0.5,  0.5), 
    vec3(0.5,  0.5,  -0.5),
    vec3(-0.5, 0.5,  -0.5)
);

uint indices[36] = uint[36](
    0,1,2,0,2,3,
    4,5,6,4,6,7,
    8,9,10,8,10,11,
    12,13,14,12,14,15,
    16,17,18,16,18,19,
    20,21,22,20,22,23
);

void fillCube(out vec3 triangles[36], vec3 center, float scale) {
    for(int i = 0; i < 36; ++i) {
        triangles[i] = center + cubeVertices[indices[i]] * scale;
    }
}


#endif // CUBE_GLSL