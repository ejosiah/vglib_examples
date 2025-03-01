#version 460

#define RADIANCE_API_ENABLED

#include "atmosphere_api.glsl"
#include "common.glsl"

layout(set = 2, binding = 0) uniform SCENE_UBO{
    mat4 inverse_projection;
    mat4 inverse_view;
    vec3 camera;
    vec3 white_point;
    vec3 earth_center;
    vec3 sun_direction;
    vec3 sun_size;
    vec3 kSphereAlbedo;
    vec3 kGroundAlbedo;
    float exposure;
    float near;
    float far;
};

layout(set = 3, binding = 0) uniform sampler3D transmittance_volume;
layout(set = 3, binding = 1) uniform sampler3D in_scattering_volume;

vec3 kSphereCenter = vec3(0.0, 1.0, 0) * km;
float kSphereRadius = 1.0 * km;

layout(location = 0) in struct {
    vec3 view_ray;
    vec2 uv;
} fs_in;

layout(location = 0) out vec4 fragColor;

float GetSunVisibility(Ray ray, vec3 point, vec3 normal){
    ray.origin = point + normal * 0.0001;
    ray.direction = sun_direction;

    Sphere sphere = Sphere(kSphereCenter, kSphereRadius);

    float t;
    return sphereTest(ray, sphere, t) ? 0 : 1;
}

void main(){
    vec3 view_ray = fs_in.view_ray;
    Ray ray = Ray(camera, normalize(view_ray));
    Sphere sphere = Sphere(kSphereCenter, kSphereRadius);
    Sphere ground = Sphere(earth_center, length(earth_center));

    float ts;
    float sphere_alpha = 0;
    vec3 sphere_radiance = vec3(0);
    if(sphereTest(ray, sphere, ts)){
        sphere_alpha = 1;
        vec3 point = ray.origin + ray.direction * ts;
        vec3 normal = normalize(point - kSphereCenter);
        vec3 sky_irradiance;
        vec3 sun_irradiance = GetSunAndSkyIrradiance(point - earth_center, normal, sun_direction, sky_irradiance);
        sphere_radiance = (kSphereAlbedo / PI) * (sky_irradiance + sun_irradiance);

        vec3 transmittance;
        vec3 in_scatter = GetSkyRadianceToPoint(camera - earth_center, point - earth_center, 0, sun_direction, transmittance);
        sphere_radiance = sphere_radiance * transmittance + in_scatter;
    }

    float tg;
    vec3 ground_radiance = vec3(0);
    float ground_alpha = 0;
    if(sphereTest(ray, ground, tg)){
        if(tg < ts){
            ground_alpha = 1;

            vec3 point = ray.origin + ray.direction * tg;

            vec3 normal = normalize(point - earth_center);
            vec3 sky_irradiance;
            vec3 sun_irradiance = GetSunAndSkyIrradiance(point - earth_center, normal, sun_direction, sky_irradiance);
            ground_radiance = (kGroundAlbedo / PI) * (sky_irradiance + sun_irradiance * GetSunVisibility(ray, point, normal));

            vec3 transmittance;
            vec3 in_scatter = GetSkyRadianceToPoint(camera - earth_center, point - earth_center, 0, sun_direction, transmittance);
            ground_radiance = ground_radiance * transmittance + in_scatter;
        }
    }

    vec3 transmittance;
    vec3 radiance = GetSkyRadiance(camera - earth_center, ray.direction, 0, sun_direction, transmittance);
    if (dot(ray.direction, sun_direction) > sun_size.y) {
        radiance = radiance + transmittance * GetSolarRadiance();
    }

    radiance = mix(radiance, sphere_radiance, sphere_alpha);
    radiance = mix(radiance, ground_radiance, ground_alpha);

    fragColor.rgb = pow(vec3(1.0) - exp(-radiance / white_point * exposure), vec3(1.0 / 2.2));
}