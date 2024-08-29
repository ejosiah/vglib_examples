#ifndef SCENE_GLSL
#define SCENE_GLSL


layout(set = 0, binding = 0) uniform Globals {
    mat4 model;
    mat4 view;
    mat4 projection;
    mat4 inverse_model;
    mat4 inverse_view;
    mat4 inverse_projection;
    mat4 mvp;

    vec3 camera;
    float exposure;

    vec3 earthCenter;
    int numPatches;

    vec3 sunDirection;
    float time;

    vec3 whitePoint;
    int height_field_texture_id;

    vec2 sunSize;
    float amplitude;
    int gradient_texture_id;

    vec3 wireframe_color;
    int wireframe_enabled;

    vec3 ocean_color;
    float wireframe_width;

    int tileSize;
    int numTiles;
} scene;

#endif // SCENE_GLSL