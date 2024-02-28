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

struct PointComp {
    vec2 position;
    int axis;
    float delta2;
    int index;
};

PointComp pointCompFrom(Point point, int index){
    return PointComp(point.position, point.axis, point.delta2, index);
}

#define QUEUE_ENTRY_TYPE PointComp
#define QUEUE_ENTRY_COMP(a, b) (a.delta2 < b.delta2)

#endif // DOMAIN_GLSL