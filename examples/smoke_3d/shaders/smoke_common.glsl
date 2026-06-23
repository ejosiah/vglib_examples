#define PI 3.1415926535
#define INV_PI (1.0 / PI)

vec3 remapToDomain(vec3 pos) {
    return mix(domain.min, domain.max, pos);
}

vec3 voxelToWorldUv(vec3 uvw) {
    return uvw;
}

float smearedHeavisideSdf(float phi) {
    if (phi > 1.5) {
        return 1;
    } else {
        if (phi < -1.5) {
            return 0;
        } else {
            return 0.5f + phi / 3.0 + 0.5f * INV_PI * sin(PI * phi / 1.5);
        }
    }
}

float mapper(float sdf, float oldVal) {
    vec3 invRes = (domain.max - domain.min) / vec3(resolution);
    float smoothingWidth = min(invRes.x, min(invRes.y, invRes.z));
    float step = 1.0 - smearedHeavisideSdf(sdf / smoothingWidth);
    return max(oldVal, (maxValue - minValue) * step + minValue);
}

float sdfBox(vec3 position, vec3 halfSize) {
    vec3 q = abs(position) - halfSize;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}
