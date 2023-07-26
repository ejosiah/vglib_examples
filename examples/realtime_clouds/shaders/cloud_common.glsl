#ifndef CLOUD_COMMON_GLSL
#define CLOUD_COMMON_GLSL

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif // PI

#define GEN_REMAP(Type) Type remap(Type x, Type a, Type b, Type c, Type d){ return mix(c, d, (x - a)/(b - a)); }

GEN_REMAP(vec4)
GEN_REMAP(vec3)
GEN_REMAP(vec2)
GEN_REMAP(float)

float densityHeightGradientForPoint(vec3 p, float height, float cloud_type){

    const vec4 stratusGrad = vec4(0.02f, 0.05f, 0.09f, 0.11f);
    const vec4 stratocumulusGrad = vec4(0.02f, 0.2f, 0.48f, 0.625f);
    const vec4 cumulusGrad = vec4(0.01f, 0.0625f, 0.78f, 1.0f);
    float stratus = 1.0f - clamp(cloud_type * 2.0f, 0, 1);
    float stratocumulus = 1.0f - abs(cloud_type - 0.5f) * 2.0f;
    float cumulus = clamp(cloud_type - 0.5f, 0, 1) * 2.0f;
    vec4 cloudGradient = stratusGrad * stratus + stratocumulusGrad * stratocumulus + cumulusGrad * cumulus;
    return smoothstep(cloudGradient.x, cloudGradient.y, height) - smoothstep(cloudGradient.z, cloudGradient.w, height);
}

float henyeyGreenstein(vec3 lightDir, vec3 viewDir, float g){
    vec3 L = normalize(lightDir);
    vec3 V = normalize(viewDir);
    float _2gcos0 = 2 * g * max(0, dot(L, V));
    float gg = g * g;
    float num = 1 - gg;
    float denum = 4 * PI * pow(1 + gg - _2gcos0, 1.5);

    return num / denum;
}

float sampleCloudDensity(vec3 p, vec3 skewed_p, float height_fraction, float cloud_type, float cloud_coverage){
    vec4 noiseComp = texture(lowFreqencyNoises, skewed_p);
    float perlinWorly = noiseComp.x;
    float wfbm = dot(vec3(.625, .25, .125), noiseComp.gba);
    float cloud = remap(perlinWorly, wfbm - 1, 1, 0, 1);
    float densityHeightField = densityHeightGradientForPoint(p, height_fraction, cloud_type);
    cloud *= densityHeightField;

    cloud = remap(cloud, 1 - cloud_coverage, 1, 0, 1);
    cloud *= cloud_coverage;

    vec3 highFreqencyNoises = texture(highFreqencyNoisesMap, p * 0.1).rgb;

    float highFreqencyFBM = dot(highFreqencyNoises, vec3(.625, .25, .125));

    float highFreqencyNoiseModifier = mix(highFreqencyFBM, 1 - highFreqencyFBM, clamp(height_fraction * 10, 0, 1));

    cloud = remap(cloud, highFreqencyNoiseModifier * 0.2, 1.0, 0.0, 1.0);

    return cloud;
}


const vec3 noise_kernel[] = {
    vec3(-0.316253, 0.147451, -0.902035),
    vec3(0.208214, 0.114857, -0.669561),
    vec3(-0.398435, -0.105541, -0.722259),
    vec3(0.0849315, -0.644174, 0.0471824),
    vec3(0.470606, 0.99835, 0.498875),
    vec3(-0.207847, -0.176372, -0.847792)
};

float sampleCloudDensityAlongCone(vec3 samplePos, vec3 direction){
    vec3 lightStep = direction * 0.1;
    float coneSpreadMultiplier = length(lightStep);
    int lod = -1;

    float density = 0;
    vec3 p = samplePos;
    for(int i = 0; i < 6; i++){
        p += lightStep * (coneSpreadMultiplier * noise_kernel[i] * float(i));

        density += sampleCloudDensity(p);
    }

    return density;
}

float lightEnergy(float sampleDensity, float percipitation, float eccentricity, vec3 samplePos, vec3 camPos, vec3 lightPos){
    float d = sampleDensity;
    float p = percipitation;
    float g = eccentricity;
    vec3 lightDir = normalize(lightPos - samplePos);
    vec3 viewDir = normalize(camPos - samplePos);
    float hg = henyeyGreenstein(lightDir, viewDir, g);

    return 2.0 * exp(-d * p) * (1 - exp(-2 * d)) * hg;
}

float sampleLightEnergy(vec3 samplePosition, vec3 direction, float cloudDensity){
    if(cloudDensity <= 0) return 1;

    float precipitation = 0;
    float sampleDensity = sampleCloudDensityAlongCone(samplePosition, direction);
    return 200 * lightEnergy(sampleDensity, precipitation, eccentricity, samplePosition, viewPosition, lightPosition);

}

#endif // CLOUD_COMMON_GLSL