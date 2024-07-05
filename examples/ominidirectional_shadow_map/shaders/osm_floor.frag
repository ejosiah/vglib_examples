#version 460

#extension GL_EXT_nonuniform_qualifier : enable

#define SHADOW_MAP globalTexturesCube[DEPTH_SHADOW_MAP_ID]

layout(constant_id = 0) const uint DEPTH_SHADOW_MAP_ID = 0;

layout(set = 0, binding = 10) uniform samplerCube globalTexturesCube[];

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
};

layout(location = 0) in struct {
    vec3 direction;
} fs_in;

layout(location = 0) out vec4 fragColor;

vec3 GetColorFromPositionAndNormal( in vec3 worldPosition, in vec3 normal );

float shadowCalculation(vec3 fragPos);

const  vec3 lightPos = vec3(0, 2, 0);
const float farPlane = 1000;

void main() {
    gl_FragDepth = 1;
    fragColor = vec4(0.2);
    vec3 o = (inverse(view) * vec4(0, 0, 0, 1)).xyz;

    vec3 n = vec3(0, 1, 0);
    vec3 rd = normalize(fs_in.direction);
    float t = -dot(n, o) / dot(n, rd);

    if(t > 0){
        vec3 p = o + rd * t;
        vec4 depth = proj * view * vec4(p, 1);
        depth /= depth.w;
        gl_FragDepth = depth.z;
  //      vec3 color = mod(floor(p.x) + floor(p.z), 2) == 0 ? vec3(1) : vec3(0.2);
        vec3 albedo =  GetColorFromPositionAndNormal(p, n);
        vec3 amb = albedo * 0.2;

        vec3 lightDir = lightPos - p;
        vec3 L = normalize(lightDir);
        vec3 N = vec3(0, 1, 0);

        float sqrDist = length(lightDir);
        sqrDist *= sqrDist;
        float radiance = max(0, dot(N, L))/sqrDist;

        vec3 col = amb + radiance * albedo * (1 - shadowCalculation(p));

        col /= col + 1;
        col = pow(col, vec3(0.4545));
        fragColor.rgb = col;
    }
}

vec3 GetColorFromPositionAndNormal( in vec3 worldPosition, in vec3 normal ) {
    const float pi = 3.141519;
    float scale = 4;
    vec3 scaledPos = scale * worldPosition.xyz * pi * 2.0;
    vec3 scaledPos2 = scale * worldPosition.xyz * pi * 2.0 / 10.0 + vec3( pi / 4.0 );
    float s = cos( scaledPos2.x ) * cos( scaledPos2.y ) * cos( scaledPos2.z );  // [-1,1] range
    float t = cos( scaledPos.x ) * cos( scaledPos.y ) * cos( scaledPos.z );     // [-1,1] range


    t = ceil( t * 0.9 );
    s = ( ceil( s * 0.9 ) + 3.0 ) * 0.25;
    vec3 colorB = vec3( 0.85, 0.85, 0.85 );
    vec3 colorA = vec3( 1, 1, 1 );
    vec3 finalColor = mix( colorA, colorB, t ) * s;

    return vec3(0.8) * finalColor;
}

float shadowCalculation(vec3 fragPos) {
    vec3 lightDir = fragPos - lightPos;
    float closestDepth = texture(SHADOW_MAP, normalize(lightDir)).r * farPlane;
    float currentDepth = length(lightDir);
    float bias = 0.05;
    return (currentDepth - bias) > closestDepth ? 1 : 0;
}