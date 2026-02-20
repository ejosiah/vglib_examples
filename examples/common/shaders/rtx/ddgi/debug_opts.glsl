#ifndef DDGI_DEBUG_OPTIONS_GLSL
#define DDGI_DEBUG_OPTIONS_GLSL

bool show_border_vs_inside() {
    return (ddgi_debug_options & 1) == 1;
}

bool show_border_type() {
    return (ddgi_debug_options & 2) == 2;
}

bool show_border_source_coordinates() {
    return (ddgi_debug_options & 4) == 4;
}

bool use_visibility() {
    return (ddgi_debug_options & 8) == 8;
}

bool use_smooth_backface() {
    return (ddgi_debug_options & 16) == 16;
}

bool use_perceptual_encoding() {
    return (ddgi_debug_options & 32) == 32;
}

bool use_backfacing_blending() {
    return (ddgi_debug_options & 64) == 64;
}

bool use_probe_offsetting() {
    return (ddgi_debug_options & 128) == 128;
}

bool use_probe_status() {
    return (ddgi_debug_options & 256) == 256;
}

bool use_infinite_bounces() {
    return (ddgi_debug_options & 512) == 512;
}

#endif // DDGI_DEBUG_OPTIONS_GLSL