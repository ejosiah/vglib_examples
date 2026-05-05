#version 460

layout(set = 1, binding = 0) uniform Material {
    vec4 colorA;
    vec4 colorB;
    vec4 lightDirTime;
    vec4 controls;
} material;

layout(location = 0) in struct {
    vec3 worldPos;
    vec3 normal;
    vec3 viewPos;
    float ring;
} fs_in;

layout(location = 0) out vec4 fragColor;

vec3 tonemap(vec3 x) {
    return x / (1.0 + x);
}

void main() {
    vec3 N = normalize(gl_FrontFacing ? fs_in.normal : -fs_in.normal);
    vec3 V = normalize(fs_in.viewPos - fs_in.worldPos);
    vec3 L = normalize(material.lightDirTime.xyz);
    float time = material.lightDirTime.w;

    float diffuse = max(dot(N, L), 0.0);
    float sky = 0.5 + 0.5 * N.y;
    float rim = pow(max(0.0, 1.0 - dot(N, V)), 4.0);
    float spec = pow(max(dot(reflect(-L, N), V), 0.0), 48.0);

    float bands = 0.5 + 0.5 * sin(length(fs_in.worldPos.xz) * material.controls.y - fs_in.worldPos.y * 0.9 + time * material.controls.z);
    float shimmer = 0.5 + 0.5 * sin((fs_in.worldPos.y + fs_in.ring * 6.0) * 5.0 - time * (2.0 + material.controls.z));

    vec3 base = mix(material.colorA.rgb, material.colorB.rgb, bands);
    vec3 accent = mix(material.colorB.rgb, material.colorA.rgb, shimmer);

    vec3 lit = base * (0.18 + diffuse * 1.2 + sky * 0.35);
    vec3 emissive = accent * (0.35 + 1.65 * smoothstep(0.6, 1.0, shimmer)) * material.colorA.w * (0.35 + material.controls.x);
    vec3 finalColor = lit + emissive + accent * rim * 1.8 + vec3(spec) * 0.9;

    finalColor = pow(tonemap(finalColor), vec3(1.0 / max(material.controls.w, 0.25)));
    fragColor = vec4(finalColor, 1.0);
}
