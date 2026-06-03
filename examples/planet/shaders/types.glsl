#ifndef TYPES_GLSL
#define TYPES_GLSL

#ifndef EnableDoublePrecisionLEB
#define EnableDoublePrecisionLEB 0
#endif

#if EnableDoublePrecisionLEB
#define LEB_DOUBLE
#define REAL_DP double
#define REAL2_DP dvec2
#define REAL3_DP dvec3
#define REAL4_DP dvec4
#define REAL4X4_DP dmat4
#else
#define REAL_DP float
#define REAL2_DP vec2
#define REAL3_DP vec3
#define REAL4_DP vec4
#define REAL4X4_DP mat4
#endif

#ifdef LEB_DOUBLE
#define LEB_DATA_TYPE double
#define LEB_DATA_TYPE3 REAL3_DP
#else
#define LEB_DATA_TYPE float
#define LEB_DATA_TYPE3 REAL3_DP
#endif

#endif // TYPES_GLSL
