#ifndef RTX_UTILS_GLSL
#define RTX_UTILS_GLSL

void orthonormalBasis(out vec3 tangent, out vec3 binormal, inout vec3 normal){
    normal = normalize(normal);
    vec3 a = abs(normal.x) > 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0);
    tangent = normalize(cross(a, normal));
    binormal = cross(normal, tangent);
}

mat3 computeTBN(vec3 normal) {
    vec3 t, b, n = normalize(normal);
    orthonormalBasis(t, b, n);
    return mat3(t, b, n);
}

vec3 checkerboard( in vec3 worldPosition, in vec3 normal, float scale) {
    const float pi = 3.141519;

    vec3 scaledPos = 2 * worldPosition.xyz * pi * 2.0;
    vec3 scaledPos2 = 2 * worldPosition.xyz * pi * 2.0 / 10.0 + vec3( pi / 4.0 );
    scaledPos *= scale;
    scaledPos *= scale;
    float s = cos( scaledPos2.x ) * cos( scaledPos2.y ) * cos( scaledPos2.z );  // [-1,1] range
    float t = cos( scaledPos.x ) * cos( scaledPos.y ) * cos( scaledPos.z );     // [-1,1] range


    t = ceil( t * 0.9 );
    s = ( ceil( s * 0.9 ) + 3.0 ) * 0.25;
    vec3 colorB = vec3( 0.85, 0.85, 0.85 );
    vec3 colorA = vec3( 1, 1, 1 );
    vec3 finalColor = mix( colorA, colorB, t ) * s;

    return vec3(0.8) * finalColor;
}

vec3 checkerboard( in vec3 worldPosition, in vec3 normal){
    return checkerboard(worldPosition, normal, 1);
}

float luminance(vec3 rgb){
    return dot(rgb, vec3(0.2126f, 0.7152f, 0.0722f));
}

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

vec3 sphericalDirection(float sin0, float cos0, float phi, vec3 x, vec3 y, vec3 z){
    return sin0 * cos(phi) * x + sin0 * sin(phi) * y + cos0 * z;
}

float linearToSrgb(float linearColor){
    if (linearColor < 0.0031308f) return linearColor * 12.92f;
    else return 1.055f * float(pow(linearColor, 1.0f / 2.4f)) - 0.055f;
}

vec3 linearToSrgb(vec3 c){
    return vec3(linearToSrgb(c.r),linearToSrgb(c.g), linearToSrgb(c.b));
}

float saturate(float v){
    return clamp(v, 0, 1);
}

float rsqrt(float x) { return inversesqrt(x); }


bool isBlack(vec3 v){
    return all(equal(v, vec3(0)));
}

bool isMirror(float metalness, float roughness){
    return metalness == 1 && roughness == 0;
}



#endif // RTX_UTILS_GLSL