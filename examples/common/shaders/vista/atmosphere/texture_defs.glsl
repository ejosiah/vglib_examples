#ifndef ATMOSPHERE_TEXTURE_DEF_GLSL
#define ATMOSPHERE_TEXTURE_DEF_GLSL

#include "atm_uniforms.glsl"

#define transmittanceLUT global_textures[nonUniformEXT(atm.transmittanceTextureIndex)]
#define multiscatteringLUT global_textures[nonUniformEXT(atm.multiScatteringTextureIndex)]
#define skyViewLUT global_textures[nonUniformEXT(atm.skyViewTextureIndex)]
#define ArealPerspectiveLUT global_textures_3d[nonUniformEXT(atm.arealPerspectiveTextureIndex)]

#endif // ATMOSPHERE_TEXTURE_DEF_GLSL