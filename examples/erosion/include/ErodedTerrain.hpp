#pragma once

#include "vista/Terrain.hpp"

class ErodedTerrain : public Terrain {
public:
    ErodedTerrain(Context& context, AtmosphereModel::Descriptor atmDescriptor, glm::ivec2 terrainSize, glm::vec2 heightScale);

protected:
    std::string renderFragmentShaderPath() const override;

    bool usesMaterialTextures() const override;

    MaterialTexturePaths materialTexturePaths() const override;
};
