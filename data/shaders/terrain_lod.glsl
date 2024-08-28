#define LOD_TYPE_DISTANCE_FROM_CAMERA 0
#define LOD_TYPE_SPHERE_DISTANCE 1

struct LodParams{
    mat4 modelView;
    mat4 projection;
    vec4 positions[4];
    float displacement[4];
    vec2 viewport;
    float minDepth;
    float maxDepth;
    float minTessLevel;
    float maxTessLevel;
    float targetTriangleWidth;
    int lodType;
};

float ceilPowerOfTwo(float value){
    int x = int(round(value));
    if (x <= 1) return 2;
    x -= 1;
    x |= (x >> 1);
    x |= (x >> 2);
    x |= (x >> 4);
    x |= (x >> 8);
    x |= (x >> 16);
    x += 1;
    return float(x);
}

float lodDistanceFromCamera(LodParams lodParams, int v0, int v1){
    vec4 p0 = lodParams.positions[v0];
    vec4 p1 = lodParams.positions[v1];

    p0.y += lodParams.displacement[v0];
    p1.y += lodParams.displacement[v1];

    float maxDepth = lodParams.maxDepth;
    float minDepth = lodParams.minDepth;
    float minTessLevel = lodParams.minTessLevel;
    float maxTessLevel = lodParams.maxTessLevel;

    p0 = lodParams.modelView * p0;
    p1 = lodParams.modelView * p1;

    float d0 = clamp((abs(p0.z) - minDepth)/(maxDepth - minDepth), 0, 1);
    float d1 = clamp((abs(p1.z) - minDepth)/(maxDepth - minDepth), 0, 1);
    float depth = (d0 + d1) * 0.5;

    float tessLevel = mix(maxTessLevel, minTessLevel, depth);

    tessLevel = ceilPowerOfTwo(tessLevel);

    return tessLevel;
}

void lodDistanceFromCamera(LodParams lodParams, out float tessLevelOuter[4], out float tessLevelInner[2]){
    tessLevelOuter[0] = lodDistanceFromCamera(lodParams, 3, 0);
    tessLevelOuter[1] = lodDistanceFromCamera(lodParams, 0, 1);
    tessLevelOuter[2] = lodDistanceFromCamera(lodParams, 1, 2);
    tessLevelOuter[3] = lodDistanceFromCamera(lodParams, 2, 3);

    tessLevelInner[0] = 0.5 * (tessLevelOuter[0] + tessLevelOuter[3]);
    tessLevelInner[1] = 0.5 * (tessLevelOuter[2] + tessLevelOuter[1]);
}

float lodSphere(LodParams lodParams, int v0, int v1){
    float targetTriangleWidth = lodParams.targetTriangleWidth;
    vec4 p0 = lodParams.positions[v0];
    vec4 p1 = lodParams.positions[v1];

    p0.y += lodParams.displacement[v0];
    p1.y += lodParams.displacement[v1];

    vec4 center = (p0 + p1) * 0.5;
    float radius = distance(p0, p1) * 0.5;

    vec4 s0 = lodParams.modelView * center + vec4(-radius, 0, 0, 1);
    vec4 s1 = lodParams.modelView * center + vec4(radius, 0, 0, 1);

    vec4 c0 = lodParams.projection * s0;
    vec4 c1 = lodParams.projection * s1;

    // perspective division
    c0 /= c0.w;
    c1 /= c1.w;

    vec2 screen0 = c0.xy * lodParams.viewport;
    vec2 screen1 = c1.xy * lodParams.viewport;

    float diameter = distance(screen0, screen1);

    float tessLevel = clamp(diameter / targetTriangleWidth, 0, lodParams.maxTessLevel);

    return tessLevel;
}

void lodSphere(LodParams lodParams, out float tessLevelOuter[4], out float tessLevelInner[2]){
    tessLevelOuter[0] = lodSphere(lodParams, 3, 0);
    tessLevelOuter[1] = lodSphere(lodParams, 0, 1);
    tessLevelOuter[2] = lodSphere(lodParams, 1, 2);
    tessLevelOuter[3] = lodSphere(lodParams, 2, 3);

    tessLevelInner[0] = 0.5 * (tessLevelOuter[0] + tessLevelOuter[3]);
    tessLevelInner[1] = 0.5 * (tessLevelOuter[2] + tessLevelOuter[1]);
}

void terrainLOD(LodParams lodParams, out float tessLevelOuter[4], out float tessLevelInner[2]){
    if(lodParams.lodType == LOD_TYPE_DISTANCE_FROM_CAMERA){
        lodDistanceFromCamera(lodParams, tessLevelOuter, tessLevelInner);
    }else if(lodParams.lodType == LOD_TYPE_SPHERE_DISTANCE){
        lodSphere(lodParams, tessLevelOuter, tessLevelInner);
    }
    else{
        while(true){} // crash shader
    }

}



