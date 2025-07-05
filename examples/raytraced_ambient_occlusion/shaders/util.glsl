#ifndef UTIL_GLSL
#define UTIL_GLSL

#define PI 3.1415926535897

vec3 offsetRay(in vec3 p, in vec3 n)
{
    const float intScale   = 256.0f;
    const float floatScale = 1.0f / 65536.0f;
    const float origin     = 1.0f / 32.0f;

    ivec3 of_i = ivec3(intScale * n.x, intScale * n.y, intScale * n.z);

    vec3 p_i = vec3(intBitsToFloat(floatBitsToInt(p.x) + ((p.x < 0) ? -of_i.x : of_i.x)),
    intBitsToFloat(floatBitsToInt(p.y) + ((p.y < 0) ? -of_i.y : of_i.y)),
    intBitsToFloat(floatBitsToInt(p.z) + ((p.z < 0) ? -of_i.z : of_i.z)));

    return vec3(abs(p.x) < origin ? p.x + floatScale * n.x : p_i.x, //
    abs(p.y) < origin ? p.y + floatScale * n.y : p_i.y, //
    abs(p.z) < origin ? p.z + floatScale * n.z : p_i.z);
}

void othonormalBasis(out vec3 tangent, out vec3 binormal, inout vec3 normal){
    normal = normalize(normal);
    vec3 a;
    if(abs(normal.x) > 0.9){
        a = vec3(0, 1, 0);
    }else {
        a = vec3(1, 0, 0);
    }
    binormal = normalize(cross(normal, a));
    tangent = cross(normal, binormal);
}

vec3 cosineSampleHemisphere(vec2 u){
    // Uniformly sample disk.
    vec3 p;
    const float r   = sqrt(u.x);
    const float phi = 2.0f * PI * u.y;
    p.x             = r * cos(phi);
    p.y             = r * sin(phi);

    // Project up to hemisphere.
    p.z = sqrt(max(0.0f, 1.0f - p.x * p.x - p.y * p.y));

    return p;
}

vec2 sampleNoise(sampler2DArray noise_texture, vec2 uv, int seed) {
    vec3 tSize = textureSize(noise_texture, 0);
    float layer = mod(frame + seed, tSize.z);
    vec2 numTiles = vec2(gl_WorkGroupSize * gl_NumWorkGroups)/tSize.xy;
    vec2 tileUV = fract(uv * numTiles);

    return texture(noise_texture, vec3(tileUV, layer)).xy;
}

#endif // UTIL_GLSL