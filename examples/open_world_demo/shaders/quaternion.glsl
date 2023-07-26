#ifndef QUATERNION_GLSL
#define QUATERNION_GLSL
// -------------------------------------------------------------------------
//    Quaternion rotations
// -------------------------------------------------------------------------


vec4 axisAngle(vec3 axis, float angle){
    float halfAngle = angle * 0.5;
    float w = cos(halfAngle);
    vec3 xyz = axis * sin(halfAngle);

    return vec4(xyz, w);
}

// Optimized point rotation using quaternion
// Source: https://gamedev.stackexchange.com/questions/28395/rotating-vector3-by-a-quaternion
vec3 rotatePoint(vec4 q, vec3 v) {
    const vec3 qAxis = vec3(q.x, q.y, q.z);
    return 2.0f * dot(qAxis, v) * qAxis + (q.w * q.w - dot(qAxis, qAxis)) * v + 2.0f * q.w * cross(qAxis, v);
}
#endif // QUATERNION_GLSL