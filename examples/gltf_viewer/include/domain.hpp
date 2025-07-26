#pragma once

enum TextureConstants{ BLACK, WHITE, NORMAL, BRDF_LUT, SHEEN_LUT, CHARLIE_LUT, COUNT};

enum class ShaderType : int {
    RayGen,
    Miss,

    ClosesHit,
    AnyHit,

    Count
};

struct UniformData {
    Camera camera;
    glm::mat4 inverse_view;
    glm::mat4 inverse_projection;
    int brdf_lut_texture_id{};
    int sheen_lut_texture_id{};
    int charlie_lut_texture_id{};
    int irradiance_texture_id{};
    int specular_texture_id{};
    int charlie_env_texture_id{};
    int framebuffer_texture_id{};
    int g_buffer_texture_id{};
    int g_buffer_image_id{};
    int discard_transmissive{};
    int environment{};
    int tone_map{1};
    int num_lights{1};
    int debug{0};
    int ibl_on{1};
    int direct_on{1};
    float ibl_intensity{1};
    int frame{0};
    int currentSample{-1};
    int maxSamples{10000};
    int maxBounces{50};
    int adaptiveSampling{1};
};

struct DebugData {
    glm::vec3 origin;
    glm::vec3 offsetOrigin;
    glm::vec3 normal;
    glm::vec3 rayDir;
    glm::vec3 viewDir;
    int reflect{0};
};

struct DebugRequest {
    glm::uvec2 mouse_position;
    int on{0};
    int counter{0};
};