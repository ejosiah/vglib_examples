#ifndef PATH_TRACING_SPECIALIZATION_GLSL
#define PATH_TRACING_SPECIALIZATION_GLSL

#define DIFFUSE_BRDF_LAMBERTIAN (1 << 0)
#define DIFFUSE_BRDF_OREN_NAYAR (1 << 1)
#define DIFFUSE_BRDF_DISNEY (1 << 2)

#define SPECULAR_BRDF_MICROFACET (1 << 3)
#define SPECLUAR_BRDF_PHONG (1 << 4)

#define BTDF_TRANSMISSION (1 << 5)

#define BRDF_DIFFUSE (DIFFUSE_BRDF_LAMBERTIAN | DIFFUSE_BRDF_LAMBERTIAN | DIFFUSE_BRDF_DISNEY)

#define BRDF_SPECULAR (SPECULAR_BRDF_MICROFACET | SPECLUAR_BRDF_PHONG)

#define NDF_FUNC_GGX 1
#define NDF_FUNC_BECKMANN 2

#define RIS_CANDIDATES_LIGHTS 8

layout(constant_id = 0) const int combine_brdf_with_fresnel = 0;

// Enable optimized G2 implementation which includes division by specular BRDF denominator (not available for all NDFs, check macro G2_DIVIDED_BY_DENOMINATOR if it was actually used)
layout(constant_id = 1) const int use_optimized_g2 = 1;

// Enable height correlated version of G2 term. Separable version will be used otherwise
layout(constant_id = 2) const int use_height_correlated_g2 = 1;

layout(constant_id = 3) const int ndf_function = NDF_FUNC_GGX;
layout(constant_id = 4) const int g2_divide_by_denomiator = 1;
layout(constant_id = 5) const int shadow_ray_in_ris = 0;

#endif
