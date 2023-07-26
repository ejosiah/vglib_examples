#ifndef MODEL_GLSL
#define MODEL_GLSL

struct Ray{
    vec3 origin;
    vec3 direction;
};

Ray generateRay(vec2 uv, vec3 origin, mat4 inverse_view, mat4 inverse_projection){
    Ray ray;
    ray.origin = origin;

    vec2 d = uv * 2.0 - 1.0;
 //   ray.origin = (inverse_view * vec4(0, 0, 0, 1)).xyz;
    vec3 target = (inverse_projection * vec4(d.x, d.y, 1, 1)).xyz;
    ray.direction = (inverse_view * vec4(normalize(target), 0)).xyz;

    ray.direction  = ray.direction;

    return ray;
}

struct Sphere{
    vec3 center;
    float radius;
};

bool sphereTest(Ray ray, Sphere sphere, out vec2 t){
    vec3 d = ray.direction;
    vec3 m = ray.origin - sphere.center;
    float rr = sphere.radius * sphere.radius;

    float a = dot(d, d);
    float b = -dot(m, d);

    vec3 l = m +( b/a) * d;

    float discr = a  * (rr - dot(l, l));

    if(discr < 0.) return false;

    float c = dot(m, m) - rr;

    if(c > 0. && b < 0.) return false;

    float q = b + sign(b) * sqrt(discr);

    t.y = q/a;
    t.x = c/q;

    return true;
}

bool cloudTest(Ray ray, Sphere inner, Sphere outer, out float tMin, out float tMax){

    vec2 t0, t1;
    bool innerHit = sphereTest(ray, inner, t0);
    bool outerHit = sphereTest(ray, outer, t1);

    if(innerHit && outerHit){
        tMin = min(t0.y, t1.y);
        tMax = max(t0.y, t1.y);
    }

    if(innerHit && !outerHit){
        tMin = max(0, min(t0.x, t0.y));
        tMax = max(t0.x, t0.y);
    }

    if(outerHit && !innerHit){
        tMin = max(0, min(t1.x, t1.y));
        tMax = max(t1.x, t1.y);
    }


    return innerHit || outerHit;
}

//bool sphereTest(Ray ray, Sphere sphere, out float t){
//    vec3 d = ray.direction;
//    vec3 m = ray.origin - sphere.center;
//    float rr = sphere.radius * sphere.radius;
//
//    float a = dot(d, d);
//    float b = dot(m, d);
//    float c = dot(m, m) - rr;
//
//    if(c > 0. && b > 0.) return false;
//
//    float discr = b * b - c * a;
//    if(discr < 0.) return false;
//    float t0 = (-b - sqrt(discr))/a;
//    float t1 = (-b + sqrt(discr))/a;
//
//    t = t0 < 0 ? t1 : t0;
//
//    return true;
//}

#endif // MODEL_GLSL