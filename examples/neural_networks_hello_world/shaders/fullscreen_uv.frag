#version 460 core

#extension GL_EXT_debug_printf : enable

layout(set = 0, binding = 0, std430) readonly buffer TrainingImages {
    float trainingImages[];
};

layout(set = 0, binding = 1, std430) readonly buffer TrainingLabels {
    int trainingLabels[];
};

layout(set = 1, binding = 0, std430) readonly buffer TestImages {
    float testImages[];
};

layout(set = 1, binding = 1, std430) readonly buffer TestLabels {
    int testLabels[];
};

layout(set = 2, binding = 1) buffer OutputImage {
    float output_image[];    // 28 * 28 pixel image
};

layout(push_constant) uniform Constants {
    vec2 mousePos;
    int mouseClicked;
    uint width;
    uint height;
    uint imageCount;
    uint offset;
} constants;

layout(location = 0) in vec2 inUv;

layout(location = 0) out vec4 fragColor;

void main() {
    const uint gridSize = 10u;

    vec2 gridUv = clamp(inUv, vec2(0.0), vec2(0.99999994)) * float(gridSize);
    uvec2 cell = uvec2(gridUv);
    vec2 localUv = fract(gridUv);

    uint imageIndex = constants.offset + cell.y * gridSize + cell.x;
    if (imageIndex >= constants.imageCount) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    uvec2 pixel = min(uvec2(localUv * vec2(constants.width, constants.height)), uvec2(constants.width - 1u, constants.height - 1u));
    uint pixelIndex = imageIndex * (constants.width * constants.height) + pixel.y * constants.width + pixel.x;
    float value = testImages[pixelIndex];

    if(constants.mouseClicked == 1) {
        const vec2 p = constants.mousePos;
        uvec2 mCell = uvec2(clamp(constants.mousePos, vec2(0.0), vec2(0.99999994)) * float(gridSize));
        if(mCell.x == cell.x && mCell.y == cell.y) {
            uint outputIndex = pixel.y * constants.width + pixel.x;
            output_image[outputIndex] = value;
        }
    }



    fragColor = vec4(vec3(value), 1.0);
}
