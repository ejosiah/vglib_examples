#version 460

layout(location = 0) in vec3 sunDir;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 position;
layout(location = 3) in vec3 sunPos;

layout(location = 0) out vec4 fragColor;

const float coeiff = 0.25;
const vec3 totalSkyLight = vec3(0.3, 0.5, 1.0);

vec3 mie(float dist, vec3 sunL){
    return max(exp(-pow(dist, 0.25)) * sunL - 0.4, 0.0);
}

void main(){
    vec3 S = normalize(sunDir);
    vec3 N = normalize(vNormal);

//    float sun = dot(S, N);
//    float sun_pos = smoothstep(0.9995, 0.99995, sun);
//    vec3 sun_color = vec3(1, 0.55, 0.15) * 1000;
//
//    vec3 deep_blue = vec3(0.3421052632, 0.9029850746, 2.311688312);
//    vec3 light_blue = deep_blue + 1;
//
//    /* Lighter around the horizon */
//    vec3 sky_color = mix(light_blue, deep_blue, smoothstep(-1.0, 0.7, dot(N, vec3(0.0, 1.0, 0.0))));
//    /* Lighter around the sun */
//    sky_color += 0.15 * mix(vec3(0.0), vec3(1.0), smoothstep(0.85, 1.0, sun));
//    sky_color += 0.2 * mix(vec3(0.0), vec3(1.0), smoothstep(0.75, 1.0, sun));
//
//    vec3 color = sky_color + sun_pos * sun_color;
//    fragColor = vec4(color, 1.0);
//
//    fragColor.rgb = fragColor.rgb/(fragColor.rgb + 1);

    float sunDistance = distance(sunPos, position);
    float scatterMult = clamp(sunDistance, 0.0, 1.0);
    float sun = clamp(1.0 - smoothstep(0.01, 0.011, scatterMult), 0.0, 1.0);

    float dist = position.y;
    dist = (coeiff * mix(scatterMult, 1.0, dist)) / dist;

    vec3 mieScatter = mie(sunDistance, vec3(1.0));

    vec3 color = dist * totalSkyLight;

    color = max(color, 0.0);

    color = max(mix(pow(color, 1.0 - color),
    color / (2.0 * color + 0.5 - color),
    clamp(sunPos.y * 2.0, 0.0, 1.0)),0.0)
    + sun + mieScatter;

    color *=  (pow(1.0 - scatterMult, 10.0) * 10.0) + 1.0;

    float underscatter = distance(sunPos.y * 0.5 + 0.5, 1.0);

    color = mix(color, vec3(0.0), clamp(underscatter, 0.0, 1.0));

    fragColor.rgb = color;
    fragColor.a = 1;
}