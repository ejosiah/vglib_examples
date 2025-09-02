#ifndef ATMOSPHERE_COMMON_DEFS_GLSL
#define ATMOSPHERE_COMMON_DEFS_GLSL

#define transmittanceTexture global_textures[nonuniformEXT(atm.transmittanceTextureIndex)]

#define transmittanceImage global_images[nonuniformEXT(atm.transmittanceImageIndex)]
#define multiScatteringImage global_images[nonuniformEXT(atm.multiScatteringImageIndex)]
#define skyViewImage global_images[nonuniformEXT(atm.skyViewImageIndex)]
#define arealPerspectiveImage global_images_3d[nonuniformEXT(atm.arealPerspectiveImageIndex)]

#define TRANSMITTANCE_TEXTURE_SIZE imageSize(transmittanceImage)
#define TRANSMITTANCE_TEXTURE_SIZE_F vec2(TRANSMITTANCE_TEXTURE_SIZE)
#define MULTI_SCATTERING_TEXTURE_SIZE imageSize(multiScatteringImage)

#define SKY_VIEW_TEXTURE_SIZE imageSize(skyViewImage)
#define SKY_VIEW_TEXTURE_SIZE_F vec2(SKY_VIEW_TEXTURE_SIZE)

#define AREAL_PERSPECTIVE_TEXTURE_SIZE imageSize(arealPerspectiveImage)
#define AREAL_PERSPECTIVE_TEXTURE_SIZE_F vec3(AREAL_PERSPECTIVE_TEXTURE_SIZE)

#define OPTICAL_DEPTH(Atmosphere, DensityProfile, R, Mu) ComputeOpticalLengthToTopAtmosphereBoundary(Atmosphere, DensityProfile, R, Mu)


#endif // ATMOSPHERE_COMMON_DEFS_GLSL