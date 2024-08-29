#pragma once

#include "common.h"
#include <glm/glm.hpp>
#include "VulkanBuffer.h"
#include "VulkanDevice.h"

static constexpr float SunAngularRadius = 0.004675;

struct SceneData {
    glm::mat4 model{1};
    glm::mat4 view{1};
    glm::mat4 projection{1};
    glm::mat4 inverse_model{1};
    glm::mat4 inverse_view{1};
    glm::mat4 inverse_projection{1};
    glm::mat4 mvp{1};

    glm::vec3 camera{0};
    float exposure{10};

    glm::vec3 earthCenter{0, - 6360 * km, 0};
    int numPatches{1};

    glm::vec3 sunDirection{glm::inversesqrt(3.f)};
    float time{0};

    glm::vec3 whitePoint{1};
    int height_field_texture_id{0};

    glm::vec2 sunSize{glm::tan(SunAngularRadius * 10), glm::cos(SunAngularRadius * 10)};
    float amplitude{100};
    int gradient_texture_id{0};

    glm::vec3 wireframe_color{1, 1, 0};
    int wireframe_enabled{0};

    glm::vec3 ocean_color{0.0056f, 0.0194f, 0.0331f};
    float wireframe_width{0.2};

    int tileSize;
    int numTiles;
};


struct Scene {
    VulkanBuffer gpu;
    SceneData* cpu;
    VkDescriptorSet descriptorSet{};

    static Scene init(const VulkanDevice& device) {
        SceneData sceneData{};
        Scene scene{};
        scene.gpu = device.createCpuVisibleBuffer(&sceneData, sizeof(SceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        scene.cpu = reinterpret_cast<SceneData*>(scene.gpu.map());

        return scene;
    }
};