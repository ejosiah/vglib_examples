#ifndef GLSL_TAA_SIMPLE
#define GLSL_TAA_SIMPLE

#define HISTORY_SAMPLING_FILTER_SINGLE 0
#define HISTORY_SAMPLING_FILTER_CATMULL_ROM 1

#define SUB_SAMPLE_FILTER_NONE 0
#define SUB_SAMPLE_FILTER_MICHELL 1
#define SUB_SAMPLE_FILTER_BLACKMAN_HARRIS 2
#define SUB_SAMPLE_FILTER_CATMULL_ROM 3

#define HISTORY_CONSTRIANT_NONE 0
#define HISTORY_CONSTRIANT_CLAMP 1
#define HISTORY_CONSTRIANT_CLIP 2
#define HISTORY_CONSTRIANT_VARIANCE_CLIP 3
#define HISTORY_CONSTRIANT_VARIANCE_CLIP_CLAMP 4

#include "filters.glsl"
#include "model.glsl"

struct ColorSample {
    vec3 value;
    vec3 min;
    vec3 max;

    // moments
    vec3 m1;
    vec3 m2;
};

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void find_closest_fragment3x3(ivec2 pos, out ivec2 closest_pos, out float closest_depth) {
    closest_pos = ivec2(0);
    closest_depth = 1;

    for(int x = -1; x <= 1; ++x){
        for(int y = -1; y <= 1; ++y){
            ivec2 candidate_pos =  clamp(pos + ivec2(x, y), ivec2(0), ivec2(scene.resolution - 1));
            float candidate_depth = texelFetch(DEPTH_BUFFER, candidate_pos, 0).x;

            if(candidate_depth < closest_depth) {
                closest_pos = candidate_pos;
                closest_depth = candidate_depth;
            }
        }
    }
}

vec3 sample_history_color(vec2 uv) {
    switch(taa.history_filter) {
        case HISTORY_SAMPLING_FILTER_SINGLE:
            return texture(HISTORY_BUFFER, uv).rgb;
        case HISTORY_SAMPLING_FILTER_CATMULL_ROM:
            return sample_texture_catmull_rom( uv, HISTORY_BUFFER, scene.resolution );
        default:
            return vec3(1, 0, 0);
    }
}

// Choose between different filters.
float subsample_filter(float value) {
    switch(taa.subsample_filter){
        case SUB_SAMPLE_FILTER_NONE:
            return value;
        case SUB_SAMPLE_FILTER_MICHELL:
            return filter_mitchell(value * 2);
        case SUB_SAMPLE_FILTER_BLACKMAN_HARRIS:
            return filter_blackman_harris(value * 2);
        case SUB_SAMPLE_FILTER_CATMULL_ROM:
            return filter_catmull_rom( value * 2);
    }
    return value;
}

ColorSample sample_color(ivec2 pos) {
    ColorSample color_sample;
    color_sample.min = vec3(10000);
    color_sample.max = vec3(-1000);
    color_sample.m1 = vec3(0);
    color_sample.m2 = vec3(0);

    vec3 total = vec3(0);
    float weight = 0;

    for (int x = -1; x <= 1; ++x){
        for (int y = -1; y <= 1; ++y){
            ivec2 sample_pos =  clamp(pos + ivec2(x, y), ivec2(0), ivec2(scene.resolution - 1));

            vec3 current_sample = texelFetch(COLOR_BUFFER, sample_pos, 0).rgb;
            vec2 subsample_position = vec2(x * 1.f, y * 1.f);
            float subsample_distance = length(subsample_position);
            float subsample_weight = subsample_filter(subsample_distance);

            total += current_sample * subsample_weight;
            weight += subsample_weight;

            color_sample.min = min(color_sample.min, current_sample);
            color_sample.max = max(color_sample.max, current_sample);

            color_sample.m1 += current_sample;
            color_sample.m2 += current_sample * current_sample;
        }
    }

    color_sample.value = total/weight;
    return color_sample;
}

// Optimized clip aabb function from Inside game.
vec4 clip_aabb(vec3 aabb_min, vec3 aabb_max, vec4 previous_sample, float average_alpha) {
    // note: only clips towards aabb center (but fast!)
    vec3 p_clip = 0.5 * (aabb_max + aabb_min);
    vec3 e_clip = 0.5 * (aabb_max - aabb_min) + 0.000000001f;

    vec4 v_clip = previous_sample - vec4(p_clip, average_alpha);
    vec3 v_unit = v_clip.xyz / e_clip;
    vec3 a_unit = abs(v_unit);
    float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

    if (ma_unit > 1.0) {
        return vec4(p_clip, average_alpha) + v_clip / ma_unit;
    }
    else {
        // point inside aabb
        return previous_sample;
    }
}


vec3 constrainHistory(ColorSample current_sample, vec3 history_color) {
    switch(taa.history_constraint){
        case HISTORY_CONSTRIANT_NONE:
        return history_color;

        case HISTORY_CONSTRIANT_CLAMP:
        return clamp(history_color, current_sample.min, current_sample.max);

        case HISTORY_CONSTRIANT_CLIP:
        return clip_aabb(current_sample.min, current_sample.max, vec4(history_color, 1.0f), 1.0f).rgb;

        case HISTORY_CONSTRIANT_VARIANCE_CLIP: {
            float rcp_sample_count = 1.0f / 9.0f;
            float gamma = 1.0f;
            vec3 mu = current_sample.m1 * rcp_sample_count;
            vec3 sigma = sqrt(abs((current_sample.m2 * rcp_sample_count) - (mu * mu)));
            vec3 minc = mu - gamma * sigma;
            vec3 maxc = mu + gamma * sigma;

            return clip_aabb(minc, maxc, vec4(history_color, 1), 1.0f).rgb;
        }
        case HISTORY_CONSTRIANT_VARIANCE_CLIP_CLAMP:
        float rcp_sample_count = 1.0f / 9.0f;
        float gamma = 1.0f;
        vec3 mu = current_sample.m1 * rcp_sample_count;
        vec3 sigma = sqrt(abs((current_sample.m2 * rcp_sample_count) - (mu * mu)));
        vec3 minc = mu - gamma * sigma;
        vec3 maxc = mu + gamma * sigma;

        vec3 clamped_history_color = clamp( history_color.rgb, current_sample.min, current_sample.max );
        return clip_aabb(minc, maxc, vec4(clamped_history_color, 1), 1.0f).rgb;
    }
    return history_color;
}

vec3 taa_rresolve_simple(ivec2 pos) {
    vec2 velocity = texelFetch(VELOCITY_BUFFER, pos, 0).xy;
    vec2 screen_uv = (vec2(pos) + 0.5)/scene.resolution;
    vec2 reprojected_uv = screen_uv - velocity;

    vec3 current_color = texture(COLOR_BUFFER, screen_uv).rgb;
    vec3 historic_color = texture(HISTORY_BUFFER, reprojected_uv).rgb;

    return mix(current_color, historic_color, 0.9);
}

void filter_blend_weights(ColorSample current_sample, vec3 history_color, out vec3 current_weight, out vec3 history_weight) {
    current_weight = vec3(0.1f);
    history_weight = vec3(1.0 - current_weight);

    // Temporal filtering
    if (taa.temporal_filtering == 1) {
        vec3 temporal_weight = clamp(abs(current_sample.max - current_sample.min) / current_sample.value, vec3(0), vec3(1));
        history_weight = clamp(mix(vec3(0.25), vec3(0.85), temporal_weight), vec3(0), vec3(1));
        current_weight = 1.0f - history_weight;
    }

    // Inverse luminance filtering
    if (taa.inverse_luminance_filtering == 1 || taa.luminance_difference_filtering == 1 ) {
        // Calculate compressed colors and luminances
        vec3 compressed_source = current_sample.value / (max(max(current_sample.value.r, current_sample.value.g), current_sample.value.b) + 1.0f);
        vec3 compressed_history = history_color / (max(max(history_color.r, history_color.g), history_color.b) + 1.0f);
        float luminance_source = luminance(compressed_source);
        float luminance_history =  luminance(compressed_history);

        if ( taa.luminance_difference_filtering == 1 ) {
            float unbiased_diff = abs(luminance_source - luminance_history) / max(luminance_source, max(luminance_history, 0.2));
            float unbiased_weight = 1.0 - unbiased_diff;
            float unbiased_weight_sqr = unbiased_weight * unbiased_weight;
            float k_feedback = mix(0.0f, 1.0f, unbiased_weight_sqr);

            history_weight = vec3(1.0 - k_feedback);
            current_weight = vec3(k_feedback);
        }

        current_weight *= 1.0 / (1.0 + luminance_source);
        history_weight *= 1.0 / (1.0 + luminance_history);
    }
}


vec3 taa_resolve(ivec2 pos) {

    ivec2 cpos; float cdepth;
    find_closest_fragment3x3(pos, cpos, cdepth);
    vec2 velocity = texelFetch(VELOCITY_BUFFER, cpos, 0).xy;
    vec2 screen_uv = (vec2(pos) + 0.5)/scene.resolution;
    vec2 reprojected_uv = screen_uv - velocity;

    ColorSample current_sample = sample_color(pos);
    vec3 historic_color = constrainHistory( current_sample, sample_history_color(reprojected_uv));

    // Guard for outside sampling
    if (any(lessThan(reprojected_uv, vec2(0.0f))) || any(greaterThan(reprojected_uv, vec2(1.0f)))) {
        return current_sample.value;
    }

    vec3 current_weight; vec3 history_weight;
    filter_blend_weights(current_sample, historic_color, current_weight, history_weight);

    vec3 result = ( current_sample.value * current_weight + historic_color * history_weight ) / max( current_weight + history_weight, 0.00001 );
    return result;
}

vec3 resolve(ivec2 pos) {
    return taa.simple == 1 ? taa_rresolve_simple(pos) : taa_resolve(pos);
}

#endif // GLSL_TAA_SIMPLE