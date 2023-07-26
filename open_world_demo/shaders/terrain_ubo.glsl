layout(set = 0, binding = 0) buffer UBO{
    mat4 model;
    mat4 view;
    mat4 projection;
    mat4 MVP;

    vec3 sunPosition;
    float heightScale;

    vec3 wireframeColor;
    int wireframe;

    vec2 numPatches;
    float wireframeWidth;
    int lod;

    float lodMinDepth;
    float lodMaxDepth;
    int minTessLevel;
    int maxTessLevel;

    vec2 viewportSize;
    int shading;
    int tessLevelColor;

    vec3 cameraPosition;
    float lodTargetTriangleWidth;

    vec3 velocity;
    int lodStrategy;

    vec3 collisionPoint;
    int invertRoughness;

    int materialId;
    int greenGrass;
    int dirt;
    int dirtRock;
    int snowFresh;
    float minZ;
    float maxZ;
    float snowStart;
    float time;
    int collision;
};