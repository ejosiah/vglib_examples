#ifndef VISTA_LEADR_GLSL
#define VISTA_LEADR_GLSL

struct LEADRDistribution {
    vec2 mean;
    float ax2;
    float ay2;
    float axy;
};

LEADRDistribution leadrDistribution(vec2 uv, float lod, float roughness) {
    vec4 moments0 = textureLod(u_SlopeMoments0Sampler, uv, lod);
    vec4 moments1 = textureLod(u_SlopeMoments1Sampler, uv, lod);

    float displacementScale = max(globals.dmapFactor, 0.0);
    float displacementScale2 = displacementScale * displacementScale;
    vec2 mean = moments0.xy * displacementScale;
    vec2 secondMoment = moments0.zw * displacementScale2;
    float crossMoment = moments1.x * displacementScale2;
    vec2 variance = max(secondMoment - mean * mean, vec2(0.0));
    float covariance = crossMoment - mean.x * mean.y;

    float alpha = max(roughness * roughness, 0.06);
    float ax2 = max(alpha * alpha + 2.0 * variance.x, 1e-4);
    float ay2 = max(alpha * alpha + 2.0 * variance.y, 1e-4);
    float maxCovariance = 0.99 * sqrt(ax2 * ay2);
    float axy = clamp(2.0 * covariance, -maxCovariance, maxCovariance);

    return LEADRDistribution(mean, ax2, ay2, axy);
}

float leadrSlopeDistribution(vec2 slope, LEADRDistribution distribution) {
    vec2 d = slope - distribution.mean;
    float det = max(distribution.ax2 * distribution.ay2 - distribution.axy * distribution.axy, 1e-8);
    float exponent = (distribution.ay2 * d.x * d.x - 2.0 * distribution.axy * d.x * d.y + distribution.ax2 * d.y * d.y) / det;

    return exp(-min(exponent, 80.0)) / (PI * sqrt(det));
}

vec3 leadrSlopeFrame(vec3 w) {
    return vec3(w.x, w.z, w.y);
}

float leadrNormalDistribution(vec3 H, LEADRDistribution distribution) {
    vec3 h = normalize(leadrSlopeFrame(H));
    if(h.z <= 1e-3) {
        return 0.0;
    }

    vec2 slope = -h.xy / h.z;
    float hz2 = h.z * h.z;
    return leadrSlopeDistribution(slope, distribution) / max(hz2 * hz2, 1e-4);
}

float leadrDirectionalAlpha(vec3 S, LEADRDistribution distribution) {
    vec3 s = normalize(leadrSlopeFrame(S));
    vec2 projected = s.xy;
    float len2 = dot(projected, projected);
    if(len2 < 1e-8) {
        return sqrt(max(0.5 * (distribution.ax2 + distribution.ay2), 1e-4));
    }

    vec2 dir = projected * inversesqrt(len2);
    float alpha2 = distribution.ax2 * dir.x * dir.x
        + 2.0 * distribution.axy * dir.x * dir.y
        + distribution.ay2 * dir.y * dir.y;
    return sqrt(max(alpha2, 1e-4));
}

float leadrSmithLambdaBeckmann(vec3 S, LEADRDistribution distribution) {
    vec3 s = normalize(leadrSlopeFrame(S));
    float cosTheta = max(s.z, 0.0);
    if(cosTheta <= 0.0) {
        return 1e6;
    }

    float sinTheta2 = max(1.0 - cosTheta * cosTheta, 0.0);
    if(sinTheta2 <= 1e-8) {
        return 0.0;
    }

    float alpha = leadrDirectionalAlpha(S, distribution);
    float tanTheta = sqrt(sinTheta2) / cosTheta;
    float a = 1.0 / max(alpha * tanTheta, 1e-4);
    if(a >= 1.6) {
        return 0.0;
    }

    float a2 = a * a;
    return (1.0 - 1.259 * a + 0.396 * a2) / (3.535 * a + 2.181 * a2);
}

float leadrSmithVisibility(vec3 V, vec3 L, LEADRDistribution distribution) {
    float lambdaV = leadrSmithLambdaBeckmann(V, distribution);
    float lambdaL = leadrSmithLambdaBeckmann(L, distribution);
    return 1.0 / (1.0 + lambdaV + lambdaL);
}

vec3 shadeFragment_LEADR(Material material, vec3 N, vec3 V, vec3 L, vec2 uv, float momentsLod, float visiblity, vec3 sunTransmittance, vec3 ambientIrradiance)
{
    vec3  albedo    = material.albedo;
    float metalness = material.metalness;
    float roughness = material.roughness;
    float ao        = material.ao;

    float shadowVis = clamp(visiblity, 0.0, 1.0);
    vec3  sunT      = clamp(sunTransmittance, 0.0, 1.0);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float specularVisibility = smoothstep(0.0, 0.05, NdotV);
    vec3  H     = normalize(V + L);

    vec3 sunIrradiance = vec3(10.0);
    vec3 radiance = sunIrradiance * shadowVis * sunT;

    vec3 F0 = mix(vec3(0.04), albedo, metalness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    LEADRDistribution leadr = leadrDistribution(uv, momentsLod, roughness);
    float NDF = min(leadrNormalDistribution(H, leadr), 64.0);
    float G = leadrSmithVisibility(V, L, leadr);

    vec3 numerator = NDF * G * F;
    vec3 specular = specularVisibility * numerator / (4.0 * NdotV * NdotL + 1e-4);

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metalness);
    vec3 diffuse = kD * albedo / PI;
    vec3 Lo = (diffuse + specular) * radiance * NdotL;
    vec3 ambient = diffuse * ambientIrradiance * ao;

    return Lo + ambient;
}

vec3 shadeFragment_LEADR(Material material, vec3 N, vec3 V, vec3 L, vec2 uv, float visiblity, vec3 sunTransmittance, vec3 ambientIrradiance)
{
    return shadeFragment_LEADR(material, N, V, L, uv, 0.0, visiblity, sunTransmittance, ambientIrradiance);
}

#endif // VISTA_LEADR_GLSL
