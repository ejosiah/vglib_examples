#ifndef RT_CONSTANTS_GLSL
#define RT_CONSTANTS_GLSL

// object types
const uint OBJECT_TYPE_NONE = 0;
const uint OBJECT_TYPE_DIFFUSE = 1;
const uint OBJECT_TYPE_GLASS = 2;
const uint OBJECT_TYPE_MIRROR = 3;

// Object masks
const uint OBJ_MASK_NONE = 0x00;
const uint OBJ_MASK_PRIMITIVES = 0x01;
const uint OBJ_MASK_LIGHTS = 0x02;
const uint OBJ_MASK_BOX = 0x04;
const uint OBJ_MASK_SPHERE = 0x04;
const uint OBJ_MASK_ALL = 0xFF;
const uint OBJ_MASK_NO_LIGHTS = OBJ_MASK_ALL & ~OBJ_MASK_LIGHTS;

// Miss shaders
const uint MAIN_MISS = 0;
const uint SHADOW_MISS = 1;
const uint PHOTON_MISS = 2;
const uint DEBUG_MISS = 3;

// SBT hit record offsets
const uint PHOTON_HIT_GROUP = 3;
const uint DEBUG_HIT_GROUP = 6;

#endif // RT_CONSTANTS_GLSL