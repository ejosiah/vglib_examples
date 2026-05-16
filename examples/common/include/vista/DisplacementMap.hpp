#pragma once

#include "Texture.h"

struct DisplacementMap {
    Texture values;
    Texture normals;
    Texture slopeMoments0;
    Texture slopeMoments1;
    uint width{};
    uint height{};
};

struct DisplacementMapInfo {
    uint values_tex_id{~0u};
    uint normal_tex_id{~0u};
    uint slope_moments0_tex_id{~0u};
    uint slope_moments1_tex_id{~0u};
    uint width{};
    uint height{};
};
