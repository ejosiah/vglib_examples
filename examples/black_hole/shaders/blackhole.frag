#version 460

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D starsTexture;

vec3 cameraPos = vec3(0.0, 0.0, 10.0);
vec3 blackHolePos = vec3(0.0, 0.0, 0.0);

float BHMass = 1.0;
float schwarzschildRadius = 0.35;

float stepSize = 0.02;
int MAX_STEPS = 800;

float backgroundZ = -12.0;
float backgroundScale = 0.04;

vec3 renderBlackHole(vec3 cameraPos, vec3 rayDir) {
    vec3 pos = cameraPos;
    vec3 dir = normalize(rayDir);

    for (int i = 0; i < MAX_STEPS; ++i) {
        vec3 toBH = pos - blackHolePos;
        float r = length(toBH);

        if (r < schwarzschildRadius) {
            return vec3(0.0); // black hole shadow
        }

        vec3 gravity = -BHMass * toBH / (r * r * r);

        dir = normalize(dir + gravity * stepSize);
        pos += dir * stepSize;
    }

    // intersect final bent ray with flat background plane
    float t = (backgroundZ - pos.z) / dir.z;

    if (t <= 0.0) {
        return vec3(0.0);
    }

    vec3 hit = pos + dir * t;

    vec2 uv = hit.xy * backgroundScale + vec2(0.5);

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return vec3(0.0);
    }

    return texture(starsTexture, uv).rgb;
}

vec3 getRayDir(vec2 uv, float fov, float aspect) {
    // convert to [-1, 1]
    vec2 p = uv * 2.0 - 1.0;

    // correct for aspect ratio
    p.x *= aspect;

    // project using FOV
    float z = -1.0 / tan(fov * 0.5);

    return normalize(vec3(p, z));
}

void main() {
    vec3 rayDir = getRayDir(uv, radians(75), 1.125);
    vec3 color = renderBlackHole(cameraPos, rayDir);
    fragColor = vec4(color, 1);
}
