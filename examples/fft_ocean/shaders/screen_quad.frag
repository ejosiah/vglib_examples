#version 460

#define DIST_TEXTURE_ID 0
#define SCOMP_REAL_TEXTURE_ID 3
#define SCOMP_IMG_TEXTURE_ID 4
#define HF_REAL_TEXTURE_ID 5
#define HF_IMG_TEXTURE_ID 6
#define HF_TEXTURE_ID 7

#define ACTION_DISPLAY_DISTRIBUTION 1
#define ACTION_DISPLAY_SPECTRAL_COMPS 2
#define ACTION_DISPLAY_FFT_HEIGHT_FIELD 3
#define ACTION_DISPLAY_HEIGHT_FIELD 4
#define ACTION_DISPLAY_HEIGHT_FIELD_X_DISPLACEMENT 5
#define ACTION_DISPLAY_HEIGHT_FIELD_Z_DISPLACEMENT 6


layout(set = 0, binding = 10) uniform sampler2D global_textures[];

layout(push_constant) uniform Constants {
    int action;
};

bool displayDistribution() {
    return action == ACTION_DISPLAY_DISTRIBUTION;
}

bool displaySpectralComponents() {
    return action == ACTION_DISPLAY_SPECTRAL_COMPS;
}

bool displayFFTHeightField() {
    return action == ACTION_DISPLAY_FFT_HEIGHT_FIELD;
}

bool displayHeightField() {
    return action == ACTION_DISPLAY_HEIGHT_FIELD;
}

bool displayHeightFieldxDisplacement() {
    return action == ACTION_DISPLAY_HEIGHT_FIELD_X_DISPLACEMENT;
}

bool displayHeightFieldzDisplacement() {
    return action == ACTION_DISPLAY_HEIGHT_FIELD_Z_DISPLACEMENT;
}

layout(location = 0) in vec2 fs_uv;

layout(location = 0) out vec4 fragColor;

void main() {
    vec2 uv = fs_uv;

    vec3 color = vec3(0);
    if(displayDistribution()) {
        color = texture(global_textures[DIST_TEXTURE_ID], uv).rgb;
    }

    if(displaySpectralComponents()) {
        vec2 c;
        c.x = texture(global_textures[SCOMP_REAL_TEXTURE_ID], uv).r;
        c.y = texture(global_textures[SCOMP_IMG_TEXTURE_ID], uv).r;

        color.rg = c;
    }

    if(displayFFTHeightField()) {
        vec2 c;
        vec2 st = uv;
        c.x = texture(global_textures[HF_REAL_TEXTURE_ID], st).r;
        c.y = texture(global_textures[HF_IMG_TEXTURE_ID], st).r;

        color.rg = c;
    }

    vec3 delta = texture(global_textures[HF_TEXTURE_ID], uv).rgb;
    if(displayHeightField()) {
        color = vec3(delta.y);
        color *= 10;
        color /= color + 1;
    }

    if(displayHeightFieldxDisplacement()) {
        color = vec3(delta.x);
        color *= 10;
        color /= color + 1;
    }

    if(displayHeightFieldzDisplacement()) {
        color = vec3(delta.z);
        color *= 10;
        color /= color + 1;
    }

    fragColor = vec4(color, 1);

}