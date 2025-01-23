#ifndef GLSL_EVALUATE_LIGHTS
#define GLSL_EVALUATE_LIGHTS

layout(set = 3, binding = 0) buffer SceneLights {
    Light slights[];
};

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


#endif // GLSL_EVALUATE_LIGHTS