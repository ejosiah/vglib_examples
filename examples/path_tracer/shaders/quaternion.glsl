#ifndef QUATERNION_GLSL
#define QUATERNION_GLSL
// -------------------------------------------------------------------------
//    Quaternion rotations
// -------------------------------------------------------------------------

// Calculates rotation quaternion from input vector to the vector (0, 0, 1)
// Input vector must be normalized!
vec4 getRotationToZAxis(vec3 v) {

    // Handle special case when input is exact or near opposite of (0, 0, 1)
    if (v.z < -0.99999f) return vec4(1.0f, 0.0f, 0.0f, 0.0f);

    return normalize(vec4(v.y, -v.x, 0.0f, 1.0f + v.z));
}

// Calculates rotation quaternion from vector (0, 0, 1) to the input vector
// Input vector must be normalized!
vec4 getRotationFromZAxis(vec3 v) {

    // Handle special case when input is exact or near opposite of (0, 0, 1)
    if (v.z < -0.99999f) return vec4(1.0f, 0.0f, 0.0f, 0.0f);

    return normalize(vec4(-v.y, v.x, 0.0f, 1.0f + v.z));
}

// Returns the quaternion with inverted rotation
vec4 invertRotation(vec4 q)
{
    return vec4(-q.x, -q.y, -q.z, q.w);
}

// Optimized point rotation using quaternion
// Source: https://gamedev.stackexchange.com/questions/28395/rotating-vector3-by-a-quaternion
vec3 rotatePoint(vec4 q, vec3 v) {
    const vec3 qAxis = vec3(q.x, q.y, q.z);
    return 2.0f * dot(qAxis, v) * qAxis + (q.w * q.w - dot(qAxis, qAxis)) * v + 2.0f * q.w * cross(qAxis, v);
}
#endif // QUATERNION_GLSL