#version 460

#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#define MATERIAL_SET 1
#define MATERIAL_BINDING_POINT 0
#define LIGHT_BINDING_POINT 1
#define LIGHT_INSTANCE_BINDING_POINT 2
#define TEXTURE_INFO_BINDING_POINT 3


#define POSITION_INDEX 0
#define NORMAL_INDEX 1
#define BASE_COLOR_INDEX 2
#define MRO_INDEX 3
#define DEPTH_BUFFER_INDEX 4

#define ALPHA_MODE_OPAQUE 0
#define ALPHA_MODE_MASK 1
#define ALPHA_MODE_BLEND 2


#define POSITION_TEXTURE global_textures[POSITION_INDEX]
#define NORMAL_TEXTURE global_textures[NORMAL_INDEX]
#define BASE_COLOR_TEXTURE global_textures[BASE_COLOR_INDEX]
#define METAL_ROUGHNESS_AO_TEXTURE global_textures[MRO_INDEX]
#define DEPTH_BUFFER_TEXTURE global_textures[DEPTH_BUFFER_INDEX]


layout(set = 2, binding = 10) uniform sampler2D global_textures[];

#include "punctual_lights.glsl"

layout(set = 3, binding = 0) buffer SceneLights {
    Light slights[];
};

layout(location = 0) in struct {
    vec2 uv;
    vec4 color;
} fs_in;


const vec3 F0 = vec3(0.04);
const vec3 F90 = vec3(1);
const float IOR = 1.5;
const float u_OcclusionStrength = 1;

struct LightingParams {
    vec4 baseColor;
    vec3 position;
    vec3 normal;
    vec3 metalRoughnessAmb;
    vec3 eyes;
};

vec3 evaluateLight(LightingParams lp);

layout(push_constant) uniform Constants {
    mat4 model;
    mat4 view;
    mat4 projection;
};

layout(location = 0) out vec4 fragColor;

void main() {
    const vec2 uv = fs_in.uv;

    vec4 baseColor = texture(BASE_COLOR_TEXTURE, uv);
    vec3 position = texture(POSITION_TEXTURE, uv).xzy;
    vec4 normal = texture(NORMAL_TEXTURE, uv);
    vec4 mro = texture(METAL_ROUGHNESS_AO_TEXTURE, uv);
    vec3 eyes = (inverse(view) * vec4(0, 0, 0, 1)).xyz;

    if(int(mro.a) == ALPHA_MODE_MASK)  {
        if(baseColor.a < int(normal.a)){
            discard;
        }
        baseColor.a = 1;
    }

    LightingParams lp = LightingParams(
    baseColor,
    position,
    normal.xyz,
    mro.xyz,
    eyes
    );

    vec3 color = evaluateLight(lp);

    color.rgb /= 1 + color.rgb;
    color.rgb = pow(color.rgb, vec3(0.4545));

    fragColor = vec4(color, baseColor.a);
    gl_FragDepth = texture(DEPTH_BUFFER_TEXTURE, uv).r;
}

vec3 evaluateLight(LightingParams lp) {
    const vec4 baseColor = lp.baseColor;
    const vec3 mro = lp.metalRoughnessAmb;
    const float metalness = clamp(mro.r, 0, 1);
    const float perceptualRoughness = clamp(mro.g, 0, 1);
    const float ao = mro.b;
    const float alphaRoughness = perceptualRoughness * perceptualRoughness;
    const vec3 f0 = mix(F0, baseColor.rgb, metalness);
    const vec3 f90 = vec3(1);
    const vec3 c_diff = mix(baseColor.rgb, vec3(0), metalness);
    const float specularWeight = 1;
    const vec3 normal = lp.normal;
    const vec3 position = lp.position;
    const vec3 eyes = lp.eyes;

    vec3 N = normalize(normal);
    vec3 V = normalize(eyes - position);

    vec3 f_specular = vec3(0.0);
    vec3 f_diffuse = vec3(0.0);

    Light light = slights[0];

    vec3 pointToLight = light.direction;
    if(light.type == LightType_Point){
        pointToLight = light.position - position;
    }

    // BSTF
    vec3 L = normalize(pointToLight);// Direction from surface point to light
    vec3 H = normalize(L + V);// Direction of the vector between L and V, called halfway vector
    float NdotL = clampedDot(N, L);
    float NdotV = clampedDot(N, V);
    float NdotH = clampedDot(N, H);
    float LdotH = clampedDot(L, H);
    float VdotH = clampedDot(V, H);

    vec3 intensity = getLighIntensity(light, pointToLight);
    vec3 l_diffuse = vec3(0.0);
    vec3 l_specular = vec3(0.0);

    l_diffuse += intensity * NdotL *  BRDF_lambertian(f0, f90, c_diff, specularWeight, VdotH);
    l_specular += intensity * NdotL * BRDF_specularGGX(f0, f90, alphaRoughness, specularWeight, VdotH, NdotL, NdotV, NdotH);

    f_diffuse += l_diffuse;
    f_specular += l_specular;

    vec3 diffuse;
    vec3 specular;

    diffuse = f_diffuse;
    specular = f_specular;

    vec3 color = diffuse + specular;

    return color;
}
