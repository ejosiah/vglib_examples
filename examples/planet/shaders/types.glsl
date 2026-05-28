#ifndef TYPES_GLSL
#define TYPES_GLSL

#ifndef EnableDoublePrecisionLEB
#define EnableDoublePrecisionLEB 1
#endif

#if EnableDoublePrecisionLEB
#define LEB_DOUBLE
#define REAL_DP double
#define REAL3_DP dvec3
#else
#define REAL_DP float
#define REAL3_DP vec3
#endif

#ifdef LEB_DOUBLE
#define LEB_DATA_TYPE double
#define LEB_DATA_TYPE3 REAL3_DP
#else
#define LEB_DATA_TYPE float
#define LEB_DATA_TYPE3 REAL3_DP
#endif

#endif // TYPES_GLSL