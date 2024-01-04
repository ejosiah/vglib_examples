#ifndef SCENE_GLSL
#define SCENE_GLSL

#ifndef SCENE_SET
#define SCENE_SET 0
#endif // SCENE_SET

layout(set = SCENE_SET, binding = 0) buffer SCENE_INFO {
    mat4 viewProjection;
    mat4 inverseViewProjection;
    mat4 lightViewProjection;

    vec3 sunDirection;
    float exposure;

    vec3 sunSize;
    float znear;

    vec3 earthCenter;
    float zfar;

    vec3 camera;

    vec3 cameraDirection;
    vec3 whitePoint;
} scene;

#endif // SCENE_GLSL