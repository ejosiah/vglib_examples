#version 460 core

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform Params {
    vec4 resolutionTime;
    vec4 colorA;
    vec4 colorB;
    vec4 controlsA;
    vec4 controlsB;
} ubo;

const float PI = 3.14159265359;
const float FAR_PLANE = 48.0;
const int MAX_STEPS = 96;

mat2 rot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, -s, s, c);
}

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

vec3 palette(float t) {
    vec3 a = ubo.colorA.rgb;
    vec3 b = ubo.colorB.rgb;
    vec3 c = mix(a.zyx, b.xyz, 0.5);
    vec3 d = vec3(0.35, 0.21, 0.13) + ubo.controlsB.xxx * 0.2;
    return a + b * cos(6.28318 * (c * t + d));
}

float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float sdTorus(vec3 p, vec2 t) {
    vec2 q = vec2(length(p.xy) - t.x, p.z);
    return length(q) - t.y;
}

vec2 mapScene(vec3 p) {
    float t = ubo.resolutionTime.z * ubo.resolutionTime.w;
    float swirl = ubo.controlsA.x;
    float density = ubo.controlsA.y;
    float pulse = ubo.controlsB.x;

    p.xy *= rot(p.z * 0.15 * swirl + t * 0.6);
    float beat = sin(t * 2.5 + p.z * 0.6) * pulse;

    vec3 repeat = p;
    repeat.z = mod(repeat.z + 1.5, 3.0) - 1.5;
    repeat.xy *= rot(t + p.z * 0.25);

    float tunnelRadius = 1.25 + 0.12 * sin(p.z * 0.4 + t * 1.3);
    float tunnel = abs(length(p.xy) - tunnelRadius) - 0.09;

    float torus = sdTorus(repeat.xzy, vec2(0.48 + beat * 0.22, 0.08));
    float core = sdBox(repeat, vec3(0.18, 0.18, 0.65));

    vec3 fins = p;
    fins.z = mod(fins.z, 1.0) - 0.5;
    fins.xy = abs(fins.xy);
    float lattice = sdBox(vec3(fins.xy - vec2(0.62, 0.04), fins.z), vec3(0.18, 0.03, 0.32));

    float scene = min(min(tunnel, torus), min(core, lattice));
    float material = 0.0;
    if (scene == torus) {
        material = 1.0;
    } else if (scene == core) {
        material = 2.0;
    } else if (scene == lattice) {
        material = 3.0;
    }

    scene /= density;
    return vec2(scene, material);
}

vec3 estimateNormal(vec3 p) {
    vec2 e = vec2(0.001, 0.0);
    return normalize(vec3(
        mapScene(p + e.xyy).x - mapScene(p - e.xyy).x,
        mapScene(p + e.yxy).x - mapScene(p - e.yxy).x,
        mapScene(p + e.yyx).x - mapScene(p - e.yyx).x
    ));
}

vec3 background(vec3 rd) {
    float horizon = pow(max(0.0, 1.0 - abs(rd.y + 0.15)), 6.0);
    vec3 bg = mix(vec3(0.01, 0.015, 0.03), vec3(0.0, 0.0, 0.0), horizon);
    vec3 stars = vec3(pow(hash13(floor(rd * 140.0)), 28.0));
    return bg + stars * 0.6;
}

void main() {
    vec2 resolution = max(ubo.resolutionTime.xy, vec2(1.0));
    vec2 uv = vUv * 2.0 - 1.0;
    uv.x *= resolution.x / resolution.y;

    float time = ubo.resolutionTime.z * ubo.resolutionTime.w;
    vec3 ro = vec3(0.0, 0.0, -6.0 - time * 6.5);
    vec3 ta = vec3(0.0, 0.0, ro.z + 4.0);

    ro.x += sin(time * 0.45) * 0.2;
    ro.y += cos(time * 0.3) * 0.16;

    vec3 ww = normalize(ta - ro);
    vec3 uu = normalize(cross(vec3(0.0, 1.0, 0.0), ww));
    vec3 vv = cross(ww, uu);
    vec3 rd = normalize(uv.x * uu + uv.y * vv + 1.7 * ww);

    float total = 0.0;
    float glow = 0.0;
    float reactor = 0.0;
    float material = 0.0;
    bool hit = false;

    for (int i = 0; i < MAX_STEPS; ++i) {
        vec3 p = ro + rd * total;
        vec2 distMat = mapScene(p);
        float d = distMat.x;

        float shell = exp(-22.0 * abs(d));
        glow += shell * (0.08 + 0.92 * hash13(floor(p * 1.7)));
        reactor += exp(-8.0 * abs(length(p.xy) - 0.35));

        if (d < 0.0015) {
            hit = true;
            material = distMat.y;
            break;
        }

        total += clamp(d, 0.015, 0.45);
        if (total > FAR_PLANE) {
            break;
        }
    }

    vec3 color = background(rd);

    if (hit) {
        vec3 p = ro + rd * total;
        vec3 n = estimateNormal(p);
        vec3 lightDir = normalize(vec3(-0.45, 0.7, -0.35));
        vec3 rimDir = normalize(vec3(0.35, -0.2, -1.0));
        float diff = max(dot(n, lightDir), 0.0);
        float rim = pow(max(0.0, 1.0 - dot(n, -rd)), 3.0);
        float spec = pow(max(dot(reflect(-lightDir, n), -rd), 0.0), 28.0);

        float materialMix = material / 3.0;
        vec3 base = palette(materialMix + total * 0.015 + time * 0.03);
        vec3 accent = mix(ubo.colorA.rgb, ubo.colorB.rgb, materialMix);
        color = base * (0.25 + diff * 1.2) + accent * (rim * 1.8 + spec * 1.5);

        float stripes = sin((atan(p.y, p.x) + p.z * 1.5) * 8.0 + time * 6.0);
        color += accent * smoothstep(0.82, 1.0, stripes) * ubo.controlsA.z;
    }

    vec3 energy = palette(total * 0.02 + time * 0.2);
    color += energy * glow * (0.12 + ubo.controlsA.z * 0.22);
    color += mix(ubo.colorA.rgb, ubo.colorB.rgb, 0.5) * reactor * ubo.controlsA.w * 0.08;

    float scan = 0.96 + 0.04 * sin(vUv.y * resolution.y * 1.35);
    float grain = (hash13(vec3(gl_FragCoord.xy, floor(time * 24.0))) - 0.5) * ubo.controlsB.y;
    color = pow(max(color * scan + grain, 0.0), vec3(0.88));
    color = mix(color, smoothstep(vec3(0.0), vec3(1.0), color), ubo.controlsB.w);

    float vignette = pow(16.0 * vUv.x * vUv.y * (1.0 - vUv.x) * (1.0 - vUv.y), 0.22 + ubo.controlsB.z * 0.65);
    color *= clamp(vignette, 0.0, 1.0);

    fragColor = vec4(color, 1.0);
}
