#ifndef SHARED_GLSL
#define SHARED_GLSL

#define PI 3.1415926535897932384626433832795

struct Domain {
    vec2 lower;
    vec2 upper;
};

layout(set = 0, binding = 0) buffer Globals {
    mat4 projection;
    Domain domain;
    float gravity;
    float spacing;
    float mass;
    float particleRadius;
    float viscousConstant;
    float gasConstant;
    int numParticles;
    int generator;
    float smoothingRadius;
    float restDensity;
    float h;
    float h2;
    float _2h3;
    float kernelMultiplier;
    float gradientMultiplier;
    int hashMapSize;
    int numCollisions;
    float time;
};

vec2 gravityForce = vec2(0, -gravity);

float safeLength(vec2 d) {
    float dd = dot(d, d);
    return dd == 0 ? 0 : sqrt(dd);
}

// smoothing kernel
float W(vec2 R) {
    const float c = kernelMultiplier;

    float r = safeLength(R);
    if(r > h) return 0;
    const float x = (h2 - r*r);
    return c * x * x * x;
}

//  gradient
vec2 dW(vec2 R) {
    const float c = gradientMultiplier;

    const float r = safeLength(R);
    if(r == 0 || r > h + 0.001) return vec2(0);

    const float x = h - r;
    return -c * x * x * R/r;
}

// laplacian
float ddW(vec2 R) {
    const float r = safeLength(R);
    if(r == 0 || r > h) return 0.f;
    const float r2 = r * r;
    const float r3 = r2 * r;
    return -(r3/_2h3) + r2/h2 + h/(2*r) - 1;
}

Domain shrink(const Domain domain, float factor){
    Domain newDomain = domain;
    newDomain.lower += factor;
    newDomain.upper -= factor;

    return newDomain;
}

#endif // SHARED_GLSL