#ifndef COMMON_GLSL
#define COMMON_GLSL

struct Ray{
    vec3 origin;
    vec3 direction;
};

struct Sphere{
    vec3 center;
    float radius;
};

bool sphereTest(Ray ray, Sphere sphere, out float t){
    vec3 d = ray.direction;
    vec3 m = ray.origin - sphere.center;
    float rr = sphere.radius * sphere.radius;

    float a = dot(d, d);
    float b = dot(m, d);
    float c = dot(m, m) - rr;

    if(c > 0. && b > 0.) return false;

    float discr = b * b - c * a;
    if(discr < 0.) return false;
    float t0 = (-b - sqrt(discr))/a;
    float t1 = (-b + sqrt(discr))/a;

    t = max(0, min(t0, t1));

    return true;
}

float exponentialDepth(float n, float f, float z, float w){
    return (n * pow(f/n, z/w))/f;
}


float exp_01_to_linear_01_depth(float z, float n, float f)
{
    float z_buffer_params_y = f / n;
    float z_buffer_params_x = 1.0f - z_buffer_params_y;

    return 1.0f / (z_buffer_params_x * z + z_buffer_params_y);
}

// ------------------------------------------------------------------

float linear_01_to_exp_01_depth(float z, float n, float f)
{
    float z_buffer_params_y = f / n;
    float z_buffer_params_x = 1.0f - z_buffer_params_y;

    return (1.0f / z - z_buffer_params_y) / z_buffer_params_x;
}

vec3 uv_to_ndc(vec3 uv, float n, float f)
{
    vec3 ndc;

    ndc.x = 2.0f * uv.x - 1.0f;
    ndc.y = 2.0f * uv.y - 1.0f;
    ndc.z = 2.0f * linear_01_to_exp_01_depth(uv.z, n, f) - 1.0f;

    return ndc;
}


#endif // COMMON_GLSL