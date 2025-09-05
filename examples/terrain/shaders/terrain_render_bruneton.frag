#version 460

#include "shared.glsl"

#define RADIANCE_API_ENABLED
#define ATMOSPHERE_PARAMS_SET 2
#define ATMOSPHERE_LUT_SET 3
#include "atmosphere/bruneton_api.glsl"
#include "tone_mapping.glsl"

layout(location = 0) in struct {
    vec3 worldPos;
    vec3 color;
    vec2 uv;
} f;

layout(location = 0) out vec4 fragColor;

float sampleShadowPCF(vec2 uv) {
    ivec2 sz    = textureSize(u_DmapShadowSampler, 0);
    vec2  texel = 1.0 / vec2(sz);

    float sum = 0.0;
    // 3×3 kernel
    for (int j = -1; j <= 1; ++j)
    for (int i = -1; i <= 1; ++i) {
        sum += texture(u_DmapShadowSampler, uv + vec2(i, j) * texel).r;
    }
    return sum / 9.0;
}

void main() {
    vec3 L = normalize(globals.lightDirection);
    vec3 normal = -1 + 2 * texture(u_NormalSampler, f.uv).xzy;
    vec3 N = normalize(normal);
    vec3 albedo = f.color;

    float visiblity = sampleShadowPCF(f.uv);

    vec3 earthCenter = atmosphereToLocalUnits(vec3(0, -ATMOSPHERE.bottom_radius, 0));
    vec3 point = f.worldPos - earthCenter;
    vec3 skyIrradiance;
    vec3 sunIrradiance = GetSunAndSkyIrradiance(point, N, L, skyIrradiance);
    vec3 radiance = (albedo / PI) * (skyIrradiance + sunIrradiance * visiblity) ;

    vec3 camera = (globals.cameraMatrix * vec4(0, 0, 0, 1)).xyz - earthCenter;
    float shadow_length = length(point - camera);
    vec3 transmittance;
    vec3 inScatter = GetSkyRadianceToPoint(camera, point, 1, L, transmittance);
    radiance = radiance * transmittance + inScatter;

    vec3 toneMapped = pow(vec3(1.0) - exp(-radiance / globals.whitePoint * globals.exposure), vec3(1.0 / 2.2));

    ivec2 fg = ivec2(gl_FragCoord);
    ivec4 mouse = globals.mouse;
    if(mouse.z == 1 && all(equal(fg, mouse.xy))) {
        vec3 wp = f.worldPos;
        vec3 tr = transmittance;
        vec3 is = inScatter;
        debugPrintfEXT("fragment info:\n");
        debugPrintfEXT("\twp: [%f, %f, %f]\n", wp.x, wp.y, wp.z);
        debugPrintfEXT("\tdistanceFromCam: %f\n", distance(camera, point));
        debugPrintfEXT("\ttr: [%f, %f, %f], is: [%f, %f, %f]\n\n", tr.x, tr.y, tr.z, is.x, is.y, is.z);
    }

//    vec3 toneMapped = tone_map(radiance, ACES);
//    toneMapped = pow(toneMapped, vec3(0.454));

    fragColor = vec4(toneMapped, 1);
}
