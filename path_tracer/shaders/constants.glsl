#ifndef CONSTANTS_GLSL
#define CONSTANTS_GLSL

#define FLT_MAX 3.402823466e+38F

#define INV_PI 0.31830988618379067153776752674503
#define INV_4PI 0.07957747154594766788444188168626
#define ONE_OVER_PI 0.31830988618379067153776752674503
#define PI 3.1415926535897932384626433832795
#define TWO_PI 6.283185307179586476925286766559
#define ONE_OVER_TWO_PI 0.15915494309189533576888376337251
#define PI_OVER_TWO 1.5707963267948966192313216916398
#define PI_OVER_FOUR 0.78539816339744830961566084581988
#define ONE_VER_SQRT_TWO 0.70710678118654752440084436210485

#define OBJECT_TYPE_CORNELL 0x1
#define OBJECT_TYPE_LIGHT 0x2
#define OBJECT_TYPE_PLANE 0x4
#define OBJECT_TYPE_DRAGON 0x8
#define ALL_OBJECTS (OBJECT_TYPE_CORNELL | OBJECT_TYPE_LIGHT | OBJECT_TYPE_PLANE | OBJECT_TYPE_DRAGON)
#define NONE_LIGHTS (ALL_OBJECTS & (~OBJECT_TYPE_LIGHT))

#define ALL_HIT_GROUP 0
#define VOLUME_HIT_GROUP 1

#define MIN_DIELECTRICS_F0 0.04f

#define BRDF_DIFFUSE 1
#define BRDF_SPECULAR 2

#define DIFFUSE_BRDF_LAMBERTIAN 1
#define DIFFUSE_BRDF_OREN_NAYAR 2
#define DIFFUSE_BRDF_DISNEY 3

#define SPECULAR_BRDF_MICROFACET 1
#define SPECLUAR_BRDF_PHONG 2

#define NDF_FUNC_GGX 1
#define NDF_FUNC_BECKMANN 2

#define RIS_CANDIDATES_LIGHTS 8

layout(constant_id = 0) const int combine_brdf_with_fresnel = 1;

// Enable optimized G2 implementation which includes division by specular BRDF denominator (not available for all NDFs, check macro G2_DIVIDED_BY_DENOMINATOR if it was actually used)
layout(constant_id = 1) const int use_optimized_g2 = 1;

// Enable height correlated version of G2 term. Separable version will be used otherwise
layout(constant_id = 2) const int use_height_correlated_g2 = 1;

layout(constant_id = 3) const int specular_brdf_type = SPECLUAR_BRDF_PHONG;
layout(constant_id = 4) const int diffuse_brdf_type = DIFFUSE_BRDF_LAMBERTIAN;
layout(constant_id = 5) const int ndf_function = NDF_FUNC_GGX;
layout(constant_id = 6) const int g2_divide_by_denomiator = 1;
layout(constant_id = 7) const int shadow_ray_in_ris = 0;

#endif // CONSTANTS_GLSL