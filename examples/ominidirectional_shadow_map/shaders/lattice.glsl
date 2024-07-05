#ifndef LATTICE_GLSL
#define LATTICE_GLSL

#define PI 3.1415926535897932384626433832795
#define TWO_PI 6.283185307179586476925286766559

vec2 rotate(float angle, vec2 x){
    mat2 m = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
    return m * x;
}

float lattice(vec2 uv) {
    float freq = 6.;

    float x = rotate(PI/6., uv).x;
    float c0 =  smoothstep(0.899, 0.9, sin(freq * TWO_PI * uv.x));

    x = rotate(-PI/6., uv).x;
    float c1 =  smoothstep(0.899, 0.9, sin(freq * TWO_PI * uv.y));

    return max(c0, c1);
}

#endif // LATTICE_GLSL