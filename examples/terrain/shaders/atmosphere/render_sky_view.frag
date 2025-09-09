#version 460 core

#include "atm_uniforms.glsl"
#define skyViewLut global_textures[nonuniformEXT(atm.skyViewTextureIndex)]

layout(set = 1, binding = 10) uniform sampler2D global_textures[];
layout(set = 1, binding = 10) uniform sampler3D global_textures_3d[];
layout(set = 1, binding = 11) uniform writeonly image2D global_images[];
layout(set = 1, binding = 11) uniform writeonly image3D global_images_3d[];

#include "common.glsl"


layout(location = 0) in struct {
    vec3 viewDirection;
} fs_in;

layout(location = 0) out vec4 fragColor;

void main() {
    vec3 WorldDir = normalize(fs_in.viewDirection);
    vec3 sunDirection = atm.sunDirection;
    AtmosphereParameters Atmosphere = GetAtmosphereParameters();
    vec3 cameraPos = atm.cameraPosition / atm.lengthUnitInMeters + vec3(0, Atmosphere.bottom_radius, 0);
//
    float viewHeight = length(cameraPos);
    vec2 uv;
    vec3 UpVector = cameraPos/viewHeight;
    float viewZenithCosAngle = dot(WorldDir, UpVector);

    vec3 sideVector = normalize(cross(UpVector, WorldDir));		// assumes non parallel vectors
    vec3 forwardVector = normalize(cross(sideVector, UpVector));	// aligns toward the sun light but perpendicular to up vector
    vec2 lightOnPlane = vec2(dot(sunDirection, forwardVector), dot(sunDirection, sideVector));
    lightOnPlane = normalize(lightOnPlane);
    float lightViewCosAngle = lightOnPlane.x;

    bool IntersectGround = raySphereIntersectNearest(cameraPos, WorldDir, vec3(0, 0, 0), Atmosphere.bottom_radius) >= 0.0f;

    SkyViewLutParamsToUv(Atmosphere, IntersectGround, viewZenithCosAngle, lightViewCosAngle, viewHeight, uv);
    vec3 luminance = texture(skyViewLut, uv).rgb + GetSunLuminance(cameraPos, WorldDir, sunDirection, Atmosphere.bottom_radius);

    fragColor = vec4(luminance, 1);
}