#pragma once

struct DisplacementMap {
    Texture values;
    Texture normals;
    uint width{};
    uint height{};
};

struct DisplacementMapInfo {
    uint values_tex_id{~0u};
    uint normal_tex_id{~0u};
    uint width{};
    uint height{};
};
