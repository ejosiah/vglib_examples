#version 460

layout(set = 0, binding = 0) uniform RenderData {
    vec4 tankMin;
    vec4 tankMax;
    vec4 waterColor;
    vec4 renderParams;
} scene;

layout(set = 0, binding = 1) uniform sampler3D densityTex;
layout(set = 0, binding = 2) uniform sampler3D obstacleTex;

layout(push_constant) uniform Camera {
    mat4 model;
    mat4 view;
    mat4 proj;
} cam;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

vec2 rayBox(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax) {
    vec3 inv = 1.0 / rd;
    vec3 t0 = (bmin - ro) * inv;
    vec3 t1 = (bmax - ro) * inv;
    vec3 lo = min(t0, t1);
    vec3 hi = max(t0, t1);
    float tNear = max(max(lo.x, lo.y), lo.z);
    float tFar = min(min(hi.x, hi.y), hi.z);
    return vec2(tNear, tFar);
}

vec3 sampleGradient(vec3 uvw) {
    vec3 e = vec3(1.0 / 48.0, 1.0 / 64.0, 1.0 / 48.0);
    return vec3(
        texture(densityTex, uvw + vec3(e.x, 0, 0)).r - texture(densityTex, uvw - vec3(e.x, 0, 0)).r,
        texture(densityTex, uvw + vec3(0, e.y, 0)).r - texture(densityTex, uvw - vec3(0, e.y, 0)).r,
        texture(densityTex, uvw + vec3(0, 0, e.z)).r - texture(densityTex, uvw - vec3(0, 0, e.z)).r
    );
}

void main() {
    vec2 clip = vec2(vUv.x * 2.0 - 1.0, vUv.y * 2.0 - 1.0);
    vec4 viewPos = inverse(cam.proj) * vec4(clip, 1.0, 1.0);
    viewPos /= viewPos.w;
    vec3 ro = (inverse(cam.view) * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    vec3 rd = normalize((inverse(cam.view) * vec4(normalize(viewPos.xyz), 0.0)).xyz);

    vec2 hit = rayBox(ro, rd, scene.tankMin.xyz, scene.tankMax.xyz);
    vec3 bg = mix(vec3(0.03, 0.05, 0.08), vec3(0.14, 0.17, 0.2), pow(max(rd.y * 0.5 + 0.5, 0.0), 1.5));

    if (hit.x > hit.y || hit.y < 0.0) {
        fragColor = vec4(bg, 1.0);
        return;
    }

    float t = max(hit.x, 0.0);
    float endT = hit.y;
    float alpha = 0.0;
    vec3 color = bg;
    bool hitSurface = false;

    for (int i = 0; i < 160 && t < endT; ++i) {
        vec3 pos = ro + rd * t;
        vec3 uvw = (pos - scene.tankMin.xyz) / (scene.tankMax.xyz - scene.tankMin.xyz);
        float density = texture(densityTex, uvw).r * (1.0 - texture(obstacleTex, uvw).r);

        if (density > scene.renderParams.x && !hitSurface) {
            vec3 normal = normalize(sampleGradient(uvw));
            vec3 lightDir = normalize(vec3(-0.4, 0.9, -0.2));
            float diff = max(dot(normal, lightDir), 0.0);
            float fresnel = pow(1.0 - max(dot(-rd, normal), 0.0), 4.0);
            vec3 surface = scene.waterColor.rgb * (0.3 + diff * 0.9) + vec3(0.8) * fresnel * 0.6;
            color = mix(color, surface, 0.85);
            hitSurface = true;
        }

        float absorb = 1.0 - exp(-density * scene.renderParams.y * scene.renderParams.z);
        color = mix(color, scene.waterColor.rgb, absorb * (1.0 - alpha));
        alpha += absorb * (1.0 - alpha) * 0.45;
        if (alpha > 0.98) {
            break;
        }
        t += scene.renderParams.z;
    }

    vec3 glass = mix(color, vec3(0.9), scene.renderParams.w * 0.08);
    float edge = smoothstep(0.0, 0.04, min(
        min(abs(t - hit.x), abs(endT - t)),
        min(min(abs(rd.x), abs(rd.y)), abs(rd.z))
    ));

    fragColor = vec4(mix(glass, vec3(0.85, 0.92, 1.0), 1.0 - edge), 1.0);
}
