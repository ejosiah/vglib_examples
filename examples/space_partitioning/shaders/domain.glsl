#ifndef DOMAIN_GLSL
#define DOMAIN_GLSL

struct Point {
    vec4 color;
    vec2 position;
    vec2 start;
    vec2 end;
    int axis;
    float delta2;
};


#endif // DOMAIN_GLSL