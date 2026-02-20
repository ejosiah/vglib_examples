#version 460

#extension GL_EXT_ray_query : require
#include "ray_query_lang.glsl"
#include "octahedral.glsl"

layout(location = 10) in flat int drawId;

layout(location = 0) in struct {
    vec4 color;
    vec3 localPos;
    vec3 position;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec3 eyes;
    vec3 lightPos;
    vec2 uv[2];
} fs_in;

#include "gltf/material_descriptor.glsl"

layout(set = 3, binding = 0) uniform accelerationStructure tlas;
#include "ray_traced_shadows.glsl"

#define LIGHT_SET 4
#define LIGHT_BINDING_POINT 0
#define LIGHT_INSTANCE_BINDING_POINT 1
#include "gltf/lights_descriptor.glsl"


#define CAMERA_SET 5
#include "camera_uniform.glsl"

#define UNIFORM_SET 6
#include "uniforms.glsl"

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 normalOut;

const int num_lights = 1;
vec3 f_diffuse = vec3(0);
vec3 f_specular = vec3(0);

float ray_traced_shadows(int lightId, int shadowIndex) {
    vec2 screen_uv = (gl_FragCoord.xy) / camera.viewportSize;
    vec3 lid = vec3(screen_uv, lightId);
    return texture(global_textures_3d[shadowIndex], lid).r;
}

void main() {
    vec4 baseColor = getBaseColor();

    if(MATERIAL.alphaMode == ALPHA_MODE_MASK)  {
        if(baseColor.a < MATERIAL.alphaCutOff){
            discard;
        }
        baseColor.a = 1;
    }

    ni = getNormalInfo();
    const Specular spec = getSpecular();
    const vec3 mro = getMRO();
    const float metalness = clamp(mro.r, 0, 1);
    const float perceptualRoughness = clamp(mro.g, 0, 1);
    const float ao = mro.b;
    const float alphaRoughness = perceptualRoughness * perceptualRoughness;
    const vec3 dielectricSpecularF0 = min(F0 * spec.color, vec3(1));
    const vec3 f0 = mix(dielectricSpecularF0, baseColor.rgb, metalness);
    const vec3 f90 = vec3(1);
    const vec3 c_diff = mix(baseColor.rgb, vec3(0), metalness);
    const float specularWeight = spec.factor;

    vec3 normal = ni.N;
    vec3 N = normalize(normal);
    vec3 V = normalize(fs_in.eyes - fs_in.position);


    for(int i = 0; i < num_lights; ++i) {
        Light light = lightAt(i);

        vec3 pointToLight;
        if (light.type != LightType_Directional){
            pointToLight = light.position - fs_in.position;
        }
        else {
            pointToLight = -light.direction;
            light.position = fs_in.position + pointToLight * 1e6;
        }

        vec3 L = normalize(pointToLight);
        vec3 H = normalize(L + V);
        float NdotL = clampedDot(N, L);
        float NdotV = clampedDot(N, V);
        float NdotH = clampedDot(N, H);
        float LdotH = clampedDot(L, H);
        float VdotH = clampedDot(V, H);

        if (NdotL > 0.0 || NdotV > 0.0){
            vec3 intensity = getLighIntensity(light, pointToLight);
            vec3 l_diffuse = intensity * NdotL *  BRDF_lambertian(f0, f90, c_diff, specularWeight, VdotH);
            vec3 l_specular = intensity * NdotL * BRDF_specularGGX(f0, f90, alphaRoughness, specularWeight, VdotH, NdotL, NdotV, NdotH);;

//            float visibility = 1 - shadow(fs_in.position, light.position, light.range, 0xff, 8);
            float visibility = ray_traced_shadows(i, light.shadowMapIndex);
//            float visibility = 1;

            f_diffuse += visibility * l_diffuse;
            f_specular += visibility * l_specular;
        }
    }

    vec2 screen_uv = (gl_FragCoord.xy + 0.5) / camera.viewportSize;
    vec3 kD = vec3(1.0);
    vec3 indirect_light = texture(global_textures[indirect_light_texture_index], screen_uv).rgb;
    vec3 diffuse_indirect = kD * ao * baseColor.rgb * indirect_light;

    vec3 diffuse = f_diffuse + diffuse_indirect;
    vec3 specular = f_specular;

    vec3 color =  diffuse + specular;

    fragColor = vec4(color, baseColor.a);
    normalOut = octEncode(normal);
}