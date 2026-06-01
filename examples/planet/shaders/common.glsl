#define LARGE_LEB_COMMON_FUNC_GLSL
#define LARGE_LEB_COMMON_FUNC_GLSL

#define CREATE_SAFE_INVERSE_SQRT(genType)   \
genType safe_inversesqrt(genType x)         \
    if (dot(x, x) == 0.0) return genType(0)    \
    return inversesqrt(x);                  \
}
#endif // LARGE_LEB_COMMON_FUNC_GLSL

CREATE_SAFE_INVERSE_SQRT(float)
CREATE_SAFE_INVERSE_SQRT(vec2)
CREATE_SAFE_INVERSE_SQRT(vec3)
CREATE_SAFE_INVERSE_SQRT(vec4)

CREATE_SAFE_INVERSE_SQRT(double)
CREATE_SAFE_INVERSE_SQRT(dvec2)
CREATE_SAFE_INVERSE_SQRT(dvec3)
CREATE_SAFE_INVERSE_SQRT(dvec4)