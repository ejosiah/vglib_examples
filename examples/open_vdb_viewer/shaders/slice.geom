#version 460

layout(points) in;
layout(triangle_strip, max_vertices=12) out;

layout(push_constant) uniform Constants {
    mat4 MVP;
    vec3 viewDir;
    vec3 scale;
    int num_slices;
};

layout(location = 0) in int layer[1];

layout(location = 0) smooth out vec3 vUV;

const float EPSILON = 1e-6;

//unit cube vertices
vec3 vertexList[8] = {
    vec3(-0.5,-0.5,-0.5),
    vec3(0.5,-0.5,-0.5),
    vec3(0.5, 0.5,-0.5),
    vec3(-0.5, 0.5,-0.5),
    vec3(-0.5,-0.5, 0.5),
    vec3(0.5,-0.5, 0.5),
    vec3(0.5, 0.5, 0.5),
    vec3(-0.5, 0.5, 0.5)
};

//unit cube edges
int edgeList[8][12] = {
    { 0,1,5,6,   4,8,11,9,  3,7,2,10 }, // v0 is front
    { 0,4,3,11,  1,2,6,7,   5,9,8,10 }, // v1 is front
    { 1,5,0,8,   2,3,7,4,   6,10,9,11}, // v2 is front
    { 7,11,10,8, 2,6,1,9,   3,0,4,5  }, // v3 is front
    { 8,5,9,1,   11,10,7,6, 4,3,0,2  }, // v4 is front
    { 9,6,10,2,  8,11,4,7,  5,0,1,3  }, // v5 is front
    { 9,8,5,4,   6,1,2,0,   10,7,11,3}, // v6 is front
    { 10,9,6,5,  7,2,3,1,   11,4,8,0 }  // v7 is front
};
const int edges[12][2] = { {0,1},{1,2},{2,3},{3,0},{0,4},{1,5},{2,6},{3,7},{4,5},{5,6},{6,7},{7,4} };

int FindAbsMax(vec3 v) {
    v = abs(v);
    int max_dim = 0;
    float val = v.x;
    if (v.y > val) {
        val = v.y;
        max_dim = 1;
    }
    if (v.z > val) {
        val = v.z;
        max_dim = 2;
    }
    return max_dim;
}

void main(){
    //get the max and min distance of each vertex of the unit cube
    //in the viewing direction
    int index = layer[0];

    //	if(index >= num_slices){
    //		EndPrimitive();
    //		return;
    //	}

    float max_dist = dot(viewDir, vertexList[0]);
    float min_dist = max_dist;
    int max_index = 0;
    int count = 0;

    for (int i = 1; i < 8; i++) {
        //get the distance between the current unit cube vertex and
        //the view vector by dot product
        float dist = dot(viewDir, vertexList[i]);

        //if distance is > max_dist, store the value and index
        if (dist > max_dist) {
            max_dist = dist;
            max_index = i;
        }

        //if distance is < min_dist, store the value
        if (dist < min_dist)
        min_dist = dist;
    }
    //find tha abs maximum of the view direction vector
    int max_dim = FindAbsMax(viewDir);

    //expand it a little bit
    min_dist -= EPSILON;
    max_dist += EPSILON;

    //local variables to store the start, direction vectors,
    //lambda intersection values
    vec3 vecStart[12];
    vec3 vecDir[12];
    float lambda[12];
    float lambda_inc[12];
    float denom = 0;

    //set the minimum distance as the plane_dist
    //subtract the max and min distances and divide by the
    //total number of slices to get the plane increment
    float plane_dist = min_dist;
    float plane_dist_inc = (max_dist - min_dist) / float(num_slices);

    //for all edges
    for (int i = 0; i < 12; i++) {
        //get the start position vertex by table lookup
        vecStart[i] = vertexList[edges[edgeList[max_index][i]][0]];

        //get the direction by table lookup
        vecDir[i] = vertexList[edges[edgeList[max_index][i]][1]] - vecStart[i];

        //do a dot of vecDir with the view direction vector
        denom = dot(vecDir[i], viewDir);

        //determine the plane intersection parameter (lambda) and
        //plane intersection parameter increment (lambda_inc)
        if (1.0 + denom != 1.0) {
            lambda_inc[i] = plane_dist_inc / denom;
            lambda[i] = (plane_dist - dot(vecStart[i], viewDir)) / denom;
        }
        else {
            lambda[i] = -1.0;
            lambda_inc[i] = 0.0;
        }
    }

    //local variables to store the intesected points
    //note that for a plane and sub intersection, we can have
    //a minimum of 3 and a maximum of 6 vertex polygon
    vec3 intersection[6];
    float dL[12];

    //determine the lambda value for all edges
    for (int e = 0; e < 12; e++)
    {
        int rIndex = num_slices - 1 - index; // reverse index;
        dL[e] = lambda[e] + rIndex * lambda_inc[e];
    }

    //if the values are between 0-1, we have an intersection at the current edge
    //repeat the same for all 12 edges
    if ((dL[0] >= 0.0) && (dL[0] < 1.0)) {
        intersection[0] = vecStart[0] + dL[0] * vecDir[0];
    }
    else if ((dL[1] >= 0.0) && (dL[1] < 1.0)) {
        intersection[0] = vecStart[1] + dL[1] * vecDir[1];
    }
    else if ((dL[3] >= 0.0) && (dL[3] < 1.0)) {
        intersection[0] = vecStart[3] + dL[3] * vecDir[3];
    }
    else return;

    if ((dL[2] >= 0.0) && (dL[2] < 1.0)) {
        intersection[1] = vecStart[2] + dL[2] * vecDir[2];
    }
    else if ((dL[0] >= 0.0) && (dL[0] < 1.0)) {
        intersection[1] = vecStart[0] + dL[0] * vecDir[0];
    }
    else if ((dL[1] >= 0.0) && (dL[1] < 1.0)) {
        intersection[1] = vecStart[1] + dL[1] * vecDir[1];
    }
    else {
        intersection[1] = vecStart[3] + dL[3] * vecDir[3];
    }

    if ((dL[4] >= 0.0) && (dL[4] < 1.0)) {
        intersection[2] = vecStart[4] + dL[4] * vecDir[4];
    }
    else if ((dL[5] >= 0.0) && (dL[5] < 1.0)) {
        intersection[2] = vecStart[5] + dL[5] * vecDir[5];
    }
    else {
        intersection[2] = vecStart[7] + dL[7] * vecDir[7];
    }
    if ((dL[6] >= 0.0) && (dL[6] < 1.0)) {
        intersection[3] = vecStart[6] + dL[6] * vecDir[6];
    }
    else if ((dL[4] >= 0.0) && (dL[4] < 1.0)) {
        intersection[3] = vecStart[4] + dL[4] * vecDir[4];
    }
    else if ((dL[5] >= 0.0) && (dL[5] < 1.0)) {
        intersection[3] = vecStart[5] + dL[5] * vecDir[5];
    }
    else {
        intersection[3] = vecStart[7] + dL[7] * vecDir[7];
    }
    if ((dL[8] >= 0.0) && (dL[8] < 1.0)) {
        intersection[4] = vecStart[8] + dL[8] * vecDir[8];
    }
    else if ((dL[9] >= 0.0) && (dL[9] < 1.0)) {
        intersection[4] = vecStart[9] + dL[9] * vecDir[9];
    }
    else {
        intersection[4] = vecStart[11] + dL[11] * vecDir[11];
    }

    if ((dL[10] >= 0.0) && (dL[10] < 1.0)) {
        intersection[5] = vecStart[10] + dL[10] * vecDir[10];
    }
    else if ((dL[8] >= 0.0) && (dL[8] < 1.0)) {
        intersection[5] = vecStart[8] + dL[8] * vecDir[8];
    }
    else if ((dL[9] >= 0.0) && (dL[9] < 1.0)) {
        intersection[5] = vecStart[9] + dL[9] * vecDir[9];
    }
    else {
        intersection[5] = vecStart[11] + dL[11] * vecDir[11];
    }

    //after all 6 possible intersection vertices are obtained,
    //we calculated the proper polygon indices by using indices of a triangular fan
    int indices[] = { 0,1,2, 0,2,3, 0,3,4, 0,4,5 };

    //Using the indices, pass the intersection vertices to the vTextureSlices vector
    for (int i = 0; i < 12; i++) {
        //	slices[12 * index + i] = intersection[indices[i]];
        gl_Layer = index;
        vec3 position = intersection[indices[i]] * scale;
        vUV = position + vec3(0.5);
        gl_Position = MVP * vec4(position, 1.0);
        EmitVertex();
    }
    EndPrimitive();
}