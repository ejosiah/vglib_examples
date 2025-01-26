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
#define NOISE_TEXTURE_INDEX 5


#define POSITION_TEXTURE global_textures[POSITION_INDEX]
#define NORMAL_TEXTURE global_textures[NORMAL_INDEX]
#define BASE_COLOR_TEXTURE global_textures[BASE_COLOR_INDEX]
#define METAL_ROUGHNESS_AO_TEXTURE global_textures[MRO_INDEX]
#define DEPTH_BUFFER_TEXTURE global_textures[DEPTH_BUFFER_INDEX]
#define NOISE_TEXTURE global_textures[NOISE_TEXTURE_INDEX]

#include "punctual_lights.glsl"


layout(set = 2, binding = 10) uniform sampler2D global_textures[];

layout(set = 3, binding = 0, scalar) buffer SceneLights {
    Light slights[];
};

#include "evaluate_light.glsl"

layout(location = 0) in struct {
    vec2 uv;
    vec4 color;
} fs_in;



layout(push_constant) uniform Constants {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec3 camera_position;
    float time;
    float fog_strength;
    float stepScale;
    int fog_noise;
    int enable_volume_shadow;
    int use_improved_integration;
    int update_transmission_first;
    int constantFog;
    int heightFog;
    int boxFog;
} u;

#define USE_IMPROVED_INTEGRATION (u.use_improved_integration == 1)
#define ENABLE_VOLUME_SHADOW (u.enable_volume_shadow == 1)
#define UPDATE_TRANSMISSION_FIRST (u.update_transmission_first == 1)

layout(location = 0) out vec4 fragColor;

float displacementSimple( vec2 p )
{
    float f;
    f  = 0.5000 * texture( NOISE_TEXTURE, p).r; p = p*2.0;
    f += 0.2500 * texture( NOISE_TEXTURE, p).r; p = p*2.0;
    f += 0.1250 * texture( NOISE_TEXTURE, p).r; p = p*2.0;
    f += 0.0625 * texture( NOISE_TEXTURE, p).r; p = p*2.0;

    return f;
}

void getParticipatingMedia(out float sigmaS, out float sigmaE, in vec3 pos)
{
    float heightFog =  0.2 * u.fog_noise *3.0*clamp(displacementSimple(pos.xz*0.005 + u.time*0.01),0.0,1.0);
    heightFog = 0.3 * clamp((heightFog- pos.y)*1.0, 0.0, 1.0) * u.heightFog;

    const float fogFactor = 1.0 + u.fog_strength * 5.0;

    const float sphereRadius = 0.5;
    float sphereFog = 1 - smoothstep(0, 0.01, length(pos - vec3(5, 2, 0)) - sphereRadius);
    sphereFog *= u.boxFog;

    const float constantFog = 0.02 * u.constantFog;

    sigmaS = constantFog + heightFog*fogFactor + sphereFog;

    const float sigmaA = 0.0;
    sigmaE = max(0.000000001, sigmaA + sigmaS); // to avoid division by zero extinction

}

float phaseFunction(){
    return 1.0/(4.0*3.14);
}

float volumetricShadow(in vec3 from, in vec3 to)
{
    if(!ENABLE_VOLUME_SHADOW) return 1;

    const float numStep = 5; // quality control. Bump to avoid shadow alisaing
    float shadow = 1.0;
    float sigmaS = 0.0;
    float sigmaE = 0.0;
    float dd = length(to-from) / numStep;
    for(float s=0.5 * u.stepScale; s<(numStep-0.1); s+=1.0 * u.stepScale)// start at 0.5 to sample at center of integral part
    {
        vec3 pos = from + (to-from)*(s/(numStep));
        getParticipatingMedia(sigmaS, sigmaE, pos);
        shadow *= exp(-sigmaE * dd);
    }
    return shadow;

}

void traceScene(vec3 r0, vec3 rD, float tMax, inout vec4 scatTrans) {
    const int numIter = 20;

    float sigmaS = 0;
    float sigmaE = 0;

    vec3 lightPos = slights[0].position;

    float transmittance = 1.0;
    vec3 scatteredLight = vec3(0);

    float d = 1.0;
    vec3 p = vec3(0);
    float dd = 1.0 * u.stepScale;

    for(int i = 0; i < numIter; ++i) {
//        if(i * d >= tMax) break;

        vec3 p = r0 + rD * d;
        getParticipatingMedia(sigmaS, sigmaE, p);

        if(USE_IMPROVED_INTEGRATION) {
            // See slide 28 at http://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/
            vec3 S = evaluateLight(p) * sigmaS * phaseFunction()* volumetricShadow(p,lightPos);// incoming light
            vec3 Sint = (S - S * exp(-sigmaE * dd)) / sigmaE; // integrate along the current step segment
            scatteredLight += transmittance * Sint; // accumulate and also take into account the transmittance from previous steps

            // Evaluate transmittance to view independentely
            transmittance *= exp(-sigmaE * dd);
        } else {
            // Basic scatering/transmittance integration
            if(UPDATE_TRANSMISSION_FIRST) {
                transmittance *= exp(-sigmaE * dd);
            }

            scatteredLight += sigmaS * evaluateLight(p) * phaseFunction() * volumetricShadow(p,lightPos) * transmittance * dd;

            if(!UPDATE_TRANSMISSION_FIRST) {
                transmittance *= exp(-sigmaE * dd);
            }
        }

//        if(dd<0.2)
//        break; // give back a lot of performance without too much visual loss
        d += dd;
    }

    scatTrans = vec4(scatteredLight, transmittance);

}

void main() {
    const vec2 uv = fs_in.uv;
    vec4 position = texture(POSITION_TEXTURE, uv);
    vec3 rO = u.camera_position;
    vec3 rD = normalize(position.xyz - u.camera_position);
    float tMax = position.w;
    vec4 scatTrans = vec4(0);

    vec3 normal = texture(NORMAL_TEXTURE, uv).xyz;
    vec4 baseColor = texture(BASE_COLOR_TEXTURE, uv);
    vec3 mro = texture(METAL_ROUGHNESS_AO_TEXTURE, uv).xyz;


    LightingParams lp = LightingParams(
        baseColor,
        position.xyz,
        normal,
        mro,
        u.camera_position,
        0
    );

    traceScene(rO, rD, position.w, scatTrans);

    vec3 color = evaluateLight(lp) * volumetricShadow(position.xyz, slights[0].position);
    color = color * scatTrans.w + scatTrans.rgb;


    color.rgb /= 1 + color.rgb;
    color.rgb = pow(color.rgb, vec3(0.4545));

    fragColor = vec4(color, baseColor.a);
    gl_FragDepth = texture(DEPTH_BUFFER_TEXTURE, uv).r;
}