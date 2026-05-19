#include "ErodedTerrain.hpp"

ErodedTerrain::ErodedTerrain(Context& context, AtmosphereModel::Descriptor atmDescriptor, glm::ivec2 terrainSize, glm::vec2 heightScale)
    : Terrain(context, atmDescriptor, terrainSize, heightScale) {
}

std::string ErodedTerrain::renderFragmentShaderPath() const {
    return "eroded_terrain.frag.spv";
}

bool ErodedTerrain::usesMaterialTextures() const {
    return true;
}

Terrain::MaterialTexturePaths ErodedTerrain::materialTexturePaths() const {
    constexpr auto base = "materials/Poliigon_GroundDirtPatchy_11261/4K/Poliigon_GroundDirtPatchy_11261_";

    return {
        .dirtAlbedo = std::string{base} + "BaseColor.jpg",
        .dirtAo = std::string{base} + "AmbientOcclusion.jpg",
        .dirtRoughness = std::string{base} + "Roughness.jpg",
        .dirtNormal = std::string{base} + "Normal.jpg",
        .grassAlbedo = std::string{base} + "BaseColor.jpg",
        .grassAo = std::string{base} + "AmbientOcclusion.jpg",
        .grassRoughness = std::string{base} + "Roughness.jpg",
        .grassNormal = std::string{base} + "Normal.jpg",
        .noise = "BlueNoiseTextures/1024_1024/LDR_RGBA_0.png",
    };
}
