#version 460

#extension GL_EXT_ray_tracing_position_fetch : enable

#include "shared.glsl"
#define MODEL_RT_SET 2
#include "../../gltf/model_rt.glsl"

layout(set = 2, binding = 0) uniform accelerationStructure tlas;

#define LIGHT_SET 3
#define LIGHT_BINDING_POINT 0
#define LIGHT_INSTANCE_BINDING_POINT 1
#include "../../gltf/lights_descriptor.glsl"

#define CAMERA_SET 4
#include "../../camera_uniform.glsl"

#define BASE_COLOR_INDEX 0
#define NORMAL_INDEX 1
#define METALLIC_ROUGHNESS_INDEX 2
#define OCCLUSION_INDEX 3
#define EMISSION_INDEX 4
#define THICKNESS_INDEX 5
#define CLEAR_COAT_INDEX 6
#define CLEAR_COAT_ROUGHNESS_INDEX 7
#define CLEAR_COAT_NORMAL_INDEX 8
#define SHEEN_COLOR_INDEX 9
#define SHEEN_ROUGHNESS_INDEX 10
#define ANISOTROPY_INDEX 11
#define SPECULAR_STRENGTH_INDEX 12
#define SPECULAR_COLOR_INDEX 13
#define IRIDESCENCE_INDEX 14
#define IRIDESCENCE_THICKNESS_INDEX 15
#define TRANSMISSION_INDEX 16
#define TEXTURE_INFO_PER_MATERIAL 20

#define MATERIAL (instance.material)

#define TEXTURE_OFFSET (MATERIAL.textureInfoOffset * TEXTURE_INFO_PER_MATERIAL)

#define BASE_COLOR_TEX_INFO textureInfos[TEXTURE_OFFSET + BASE_COLOR_INDEX]
#define NORMAL_TEX_INFO textureInfos[TEXTURE_OFFSET + NORMAL_INDEX]
#define METAL_ROUGHNESS_TEX_INFO textureInfos[TEXTURE_OFFSET + METALLIC_ROUGHNESS_INDEX]
#define OCCLUSION_TEX_INFO textureInfos[TEXTURE_OFFSET + OCCLUSION_INDEX]


#define EMISSION_TEX_INFO textureInfos[TEXTURE_OFFSET + EMISSION_INDEX]
#define THICKNESS_TEX_INFO textureInfos[TEXTURE_OFFSET + THICKNESS_INDEX]

#define CLEAR_COAT_TEX_INFO textureInfos[TEXTURE_OFFSET + CLEAR_COAT_INDEX]
#define CLEAR_COAT_ROUGHNESS_TEX_INFO textureInfos[TEXTURE_OFFSET + CLEAR_COAT_ROUGHNESS_INDEX]
#define CLEAR_COAT_NORMAL_TEX_INFO textureInfos[TEXTURE_OFFSET + CLEAR_COAT_NORMAL_INDEX]

#define SHEEN_COLOR_TEX_INFO textureInfos[TEXTURE_OFFSET + SHEEN_COLOR_INDEX]
#define SHEEN_ROUGHNESS_TEX_INFO textureInfos[TEXTURE_OFFSET + SHEEN_ROUGHNESS_INDEX]

#define ANISOTROPY_TEX_INFO textureInfos[TEXTURE_OFFSET + ANISOTROPY_INDEX]

#define SPECULAR_STRENGTH_TEX_INFO textureInfos[TEXTURE_OFFSET + SPECULAR_STRENGTH_INDEX]
#define SPECULAR_COLOR_TEX_INFO textureInfos[TEXTURE_OFFSET + SPECULAR_COLOR_INDEX]

#define IRIDESCENCE_TEX_INFO textureInfos[TEXTURE_OFFSET + IRIDESCENCE_INDEX]
#define IRIDESCENCE_THICKNESS_TEX_INFO textureInfos[TEXTURE_OFFSET + IRIDESCENCE_THICKNESS_INDEX]

#define TRANSMISSION_TEX_INFO textureInfos[TEXTURE_OFFSET + TRANSMISSION_INDEX]

#define BASE_COLOR_TEXTURE global_textures[nonuniformEXT(BASE_COLOR_TEX_INFO.index)]
#define NORMAL_TEXTURE global_textures[nonuniformEXT(NORMAL_TEX_INFO.index)]
#define METAL_ROUGHNESS_TEXTURE global_textures[nonuniformEXT(METAL_ROUGHNESS_TEX_INFO.index)]
#define OCCLUSION_TEXTURE global_textures[nonuniformEXT(OCCLUSION_TEX_INFO.index)]

#define EMISSION_TEXTURE global_textures[nonuniformEXT(EMISSION_TEX_INFO.index)]
#define THICKNESS_TEXTURE global_textures[nonuniformEXT(THICKNESS_TEX_INFO.index)]

#define CLEAR_COAT_TEXTURE global_textures[nonuniformEXT(CLEAR_COAT_TEX_INFO.index)]
#define CLEAR_COAT_ROUGHNESS_TEXTURE global_textures[nonuniformEXT(CLEAR_COAT_ROUGHNESS_TEX_INFO.index)]
#define CLEAR_COAT_NORMAL_TEXTURE global_textures[nonuniformEXT(CLEAR_COAT_NORMAL_TEX_INFO.index)]

#define SHEEN_COLOR_TEXTURE global_textures[nonuniformEXT(SHEEN_COLOR_TEX_INFO.index)]
#define SHEEN_ROUGHNESS_TEXTURE global_textures[nonuniformEXT(SHEEN_ROUGHNESS_TEX_INFO.index)]

#define ANISOTROPY_TEXTURE global_textures[nonuniformEXT(ANISOTROPY_TEX_INFO.index)]

#define SPECULAR_STRENGTH_TEXTURE global_textures[nonuniformEXT(SPECULAR_STRENGTH_TEX_INFO.index)]
#define SPECULAR_COLOR_TEXTURE global_textures[nonuniformEXT(SPECULAR_COLOR_TEX_INFO.index)]

#define IRIDESCENCE_TEXTURE global_textures[nonuniformEXT(IRIDESCENCE_TEX_INFO.index)]
#define IRIDESCENCE_THICKNESS_TEXTURE global_textures[nonuniformEXT(IRIDESCENCE_THICKNESS_TEX_INFO.index)]

#define TRANSMISSION_TEXTURE global_textures[nonuniformEXT(TRANSMISSION_TEX_INFO.index)]

#define u_GGXLUT global_textures[nonuniformEXT(brdf_lut_texture_id)]
#define u_CharlieLUT global_textures[nonuniformEXT(charlie_lut_texture_id)]
#define u_SheenELUT global_textures[nonuniformEXT(sheen_lut_texture_id)]
#define u_GGXEnvSampler global_textures[nonuniformEXT(specular_texture_id)]
#define u_CharlieEnvSampler global_textures[nonuniformEXT(charlie_env_texture_id)]
#define u_LambertianEnvSampler global_textures[nonuniformEXT(irradiance_texture_id)]
#define u_TransmissionFramebufferSampler global_textures[nonuniformEXT(framebuffer_texture_id)]
#define MODEL_MATRIX (model * meshes[nonuniformEXT(drawId)].model)


layout(location = 0) rayPayloadIn RayPayload payload;
hitAttribute vec2 bc;

InstanceData instance;

float u = 1 - bc.x - bc.y;
float v = bc.x;
float w = bc.y;

const vec3 F0 = vec3(0.04);
const vec3 F90 = vec3(1);
const float IOR = 1.5;
const float u_OcclusionStrength = 1;
NormalInfo ni;

void main() {
    if(gl_HitKind == gl_HitKindBackFacingTriangle) {
        payload.radiance = vec3(0);
        payload.distance = -(gl_RayTmin + gl_HitT) * 0.2;

        return;
    }

    load(instance, bc, gl_InstanceCustomIndex, gl_PrimitiveID);

    vec4 baseColor = getBaseColor();

    // TODO skip ALPHA
//    if(MATERIAL.alphaMode == ALPHA_MODE_MASK)  {
//        if(baseColor.a < MATERIAL.alphaCutOff){
//            discard;
//        }
//        baseColor.a = 1;
//    }

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
    const vec3 world_position = instance.position.xyz;

    vec3 normal = ni.N;
    vec3 N = normalize(normal);

    vec3 diffuse = vec3(0);
    for(int i = 0; i < num_lights; ++i) {
        Light light = lightAt(i);

        vec3 pointToLight;
        if (light.type != LightType_Directional){
            pointToLight = light.position - world_position;
        }
        else {
            pointToLight = -light.direction;
            light.position = world_position + pointToLight * 1e6;
        }

        vec3 L = normalize(pointToLight);
        vec3 V = normalize(camera.position - world_position);

        vec3 H = normalize(L + V);
        float NdotL = clampedDot(N, L);
        float NdotV = clampedDot(N, V);
        float VdotH = clampedDot(V, H);

        if (NdotL > 0.0 || NdotV > 0.0){
            vec3 intensity = getLighIntensity(light, pointToLight);
            diffuse += intensity * NdotL *  BRDF_lambertian(f0, f90, c_diff, specularWeight, VdotH);
        }
        
        if(use_infinite_bounces()) {
            diffuse += c_diff * sample_irradiance(world_position, N, camera.position) * infinite_bounces_multiplier;
        }
    }
    payload.radiance = diffuse;
    payload.distance = gl_RayTminEXT + gl_HitTEXT;

}

#include "../../gltf/model_impl.glsl"
