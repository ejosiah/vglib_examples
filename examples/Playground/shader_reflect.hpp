#pragma once

#include <spirv-headers/spirv.h>
#include <fmt/format.h>
#include "spirv_reflect.h"

#include <vector>
#include <span>
#include <filesystem>
#include <fstream>


class FileReader {
public:
    FileReader() = default;

    std::span<char> readFully(const std::filesystem::path& path) {
        std::ifstream fin{path.string(), std::ios::binary | std::ios::ate};
        if(!fin.is_open()){
            throw std::runtime_error{fmt::format("unable to open: {}", path.string())};
        }
        auto size = fin.tellg();
        fin.seekg(0);
        _buffer.resize(size);
        fin.read(_buffer.data(), size);

        return _buffer;
    }

private:
    std::vector<char> _buffer;

};

std::string toString(SpvReflectShaderStageFlagBits stage) {
    switch(stage){
        case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT:
            return "Vertex";
        case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT:
            return "Fragment";
        default:
            throw std::runtime_error{"unknown shader stage"};
    }
}

inline int shader_reflect(int argn, char** argv) {
    if(argn < 2) {
        fmt::print("Usage: shader path");
        return 1;
    }
    auto shader_path = argv[1];
    FileReader fileReader{};
    auto shader = fileReader.readFully(shader_path);

    spv_reflect::ShaderModule shaderModule{shader.size(), shader.data()};
    fmt::print("Shader info\n");

    auto stage = shaderModule.GetShaderStage();
    fmt::print("\tShader stage: {}\n", toString(stage));

    uint32_t count;
    shaderModule.EnumerateDescriptorSets(&count, nullptr);
    fmt::print("\tnum descriptor sets: {}\n", count);

    shaderModule.EnumerateInputVariables(&count, nullptr);
    fmt::print("\tInput variables: {}\n", count);
    return 0;
}