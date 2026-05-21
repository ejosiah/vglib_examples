#include "vista/DisplacementMapGenerator.hpp"
#include "Barrier.hpp"
#include "L2DFileDialog.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <imgui.h>
#include <nlohmann/json.hpp>

namespace {
    constexpr VkSamplerAddressMode DepthMapAddressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    constexpr VkPipelineStageFlags2 GeneratedTextureReadStages =
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

    constexpr VkAccessFlags2 GeneratedTextureReadAccess = VK_ACCESS_2_SHADER_READ_BIT;

    bool sliderUint(const char* label, uint& value, int min, int max) {
        int current = static_cast<int>(value);
        if(!ImGui::SliderInt(label, &current, min, max)) {
            return false;
        }

        value = static_cast<uint>(std::clamp(current, min, max));
        return true;
    }

    uint nextPowerOfTwo(uint value) {
        if(value <= 1) {
            return 1;
        }

        --value;
        value |= value >> 1;
        value |= value >> 2;
        value |= value >> 4;
        value |= value >> 8;
        value |= value >> 16;
        return value + 1;
    }

    uint log2PowerOfTwo(uint value) {
        uint result = 0;
        while(value > 1) {
            value >>= 1;
            ++result;
        }
        return result;
    }

    static constexpr std::array<const char*, 2> BlendLayerSources{ "Noise", "FFT" };

    static constexpr std::array<const char*, 6> DisplacementMethodStateNames{
        "none",
        "file",
        "fault_formation",
        "noise",
        "fft",
        "blend"
    };

    static constexpr std::array<const char*, 2> BlendLayerSourceStateNames{
        "noise",
        "fft"
    };

    static constexpr std::array<const char*, 24> BlendModeStateNames{
        "normal",
        "dissolve",
        "darken",
        "multiply",
        "color_burn",
        "linear_burn",
        "darker_color",
        "lighten",
        "screen",
        "color_dodge",
        "linear_dodge",
        "lighter_color",
        "overlay",
        "soft_light",
        "hard_light",
        "vivid_light",
        "linear_light",
        "pin_light",
        "hard_mix",
        "difference",
        "exclusion",
        "subtract",
        "divide",
        "max_base_negative_layer"
    };

    static constexpr std::array<const char*, 24> BlendModes{
        "Normal",
        "Dissolve",
        "Darken",
        "Multiply",
        "Color Burn",
        "Linear Burn",
        "Darker Color (Min)",
        "Lighten",
        "Screen",
        "Color Dodge",
        "Linear Dodge (Add)",
        "Lighter Color (Max)",
        "Overlay",
        "Soft Light",
        "Hard Light",
        "Vivid Light",
        "Linear Light",
        "Pin Light",
        "Hard Mix",
        "Difference",
        "Exclusion",
        "Subtract",
        "Divide",
        "Max (B, -L)"
    };

    nlohmann::json toJson(glm::vec2 value) {
        return nlohmann::json::array({ value.x, value.y });
    }

    glm::vec2 readVec2(const nlohmann::json& json, const char* key, glm::vec2 fallback) {
        if(!json.is_object()) {
            return fallback;
        }

        auto itr = json.find(key);
        if(itr == json.end() || !itr->is_array() || itr->size() < 2) {
            return fallback;
        }

        return {
            (*itr)[0].get<float>(),
            (*itr)[1].get<float>()
        };
    }

    float readFloat(const nlohmann::json& json, const char* key, float fallback) {
        if(!json.is_object()) {
            return fallback;
        }

        auto itr = json.find(key);
        return itr != json.end() && itr->is_number() ? itr->get<float>() : fallback;
    }

    uint readUint(const nlohmann::json& json, const char* key, uint fallback) {
        if(!json.is_object()) {
            return fallback;
        }

        auto itr = json.find(key);
        if(itr == json.end() || !itr->is_number_integer()) {
            return fallback;
        }

        const auto value = itr->get<int64_t>();
        return value >= 0 ? static_cast<uint>(value) : fallback;
    }

    int readInt(const nlohmann::json& json, const char* key, int fallback) {
        if(!json.is_object()) {
            return fallback;
        }

        auto itr = json.find(key);
        return itr != json.end() && itr->is_number_integer() ? itr->get<int>() : fallback;
    }

    bool readBool(const nlohmann::json& json, const char* key, bool fallback) {
        if(!json.is_object()) {
            return fallback;
        }

        auto itr = json.find(key);
        return itr != json.end() && itr->is_boolean() ? itr->get<bool>() : fallback;
    }

    std::array<float, 6> readFrequencies(const nlohmann::json& json, std::array<float, 6> fallback) {
        auto itr = json.find("frequencies");
        if(itr == json.end() || !itr->is_array()) {
            return fallback;
        }

        const auto count = std::min(itr->size(), fallback.size());
        for(size_t i = 0; i < count; ++i) {
            if((*itr)[i].is_number()) {
                fallback[i] = (*itr)[i].get<float>();
            }
        }
        return fallback;
    }

    template<size_t N>
    const char* enumName(uint index, const std::array<const char*, N>& names) {
        return names[std::min<uint>(index, static_cast<uint>(N - 1))];
    }

    template<size_t N>
    uint readEnumIndex(const nlohmann::json& json, const char* key, const std::array<const char*, N>& names, uint fallback) {
        if(!json.is_object()) {
            return fallback;
        }

        auto itr = json.find(key);
        if(itr == json.end()) {
            return fallback;
        }

        if(itr->is_string()) {
            const auto value = itr->get<std::string>();
            for(uint i = 0; i < names.size(); ++i) {
                if(value == names[i]) {
                    return i;
                }
            }
        }else if(itr->is_number_integer()) {
            const auto value = itr->get<int64_t>();
            if(value >= 0) {
                return std::min<uint>(static_cast<uint>(value), static_cast<uint>(N - 1));
            }
        }

        return fallback;
    }

    fs::path jsonPath(fs::path path) {
        if(path.extension().empty()) {
            path.replace_extension(".json");
        }
        return path;
    }
}

DisplacementMapGenerator::DisplacementMapGenerator(Context &context, DisplacementMethod method, uint width, uint height, std::string path)
    :m_context{&context},
     m_method{method},
     m_displacementMap{.width = width,.height = height },
     m_info{
        .values_tex_id = context.dmap_tex_index,
        .normal_tex_id = context.dmap_normal_tex_index,
        .slope_moments0_tex_id = context.dmap_slope_moments0_tex_index,
        .slope_moments1_tex_id = context.dmap_slope_moments1_tex_index,
        .width = width,
        .height = height
    },
    m_path{path}
    {
        std::strncpy(m_stateFilePath.data(), "displacement_generator.json", m_stateFilePath.size() - 1);

        m_blendLayers.resize(2);
        m_blendLayers[0].blendMode = BlendMode::Normal;
        m_blendLayers[0].opacity = 1.0f;
        m_blendLayers[0].noise.seed = {137.0f, 941.0f};
        m_blendLayers[0].noise.baseFrequency = 2.5f;
        m_blendLayers[0].noise.octaves = 6;

        m_blendLayers[1].blendMode = BlendMode::Overlay;
        m_blendLayers[1].opacity = 0.45f;
        m_blendLayers[1].noise.seed = {719.0f, 113.0f};
        m_blendLayers[1].noise.baseFrequency = 9.0f;
        m_blendLayers[1].noise.gain = 0.42f;
        m_blendLayers[1].noise.octaves = 5;
        m_blendLayers[1].fft.seed = {491.0f, 223.0f};
        m_blendLayers[1].fft.amplitude = 0.16f;
        m_blendLayers[1].fft.spectralPower = 2.4f;
        m_blendLayers[1].fft.frequencies = {2.0f, 12.0f, 48.0f, 192.0f, 256.0f, 384.0f};
        m_blendLayers[1].fft.frequencyCount = 4;
    }

void DisplacementMapGenerator::init() {
    createComputePipelines();
    loadDisplacementMap();
    device().graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
       exec(commandBuffer);
    });
}

void DisplacementMapGenerator::exec(VkCommandBuffer commandBuffer) {
    switch(m_method){
        case DisplacementMethod::None:
            noneDisplacementMap(commandBuffer);
            break;
        case DisplacementMethod::File:
            computeFileDisplacementMap(commandBuffer);
            break;
        case DisplacementMethod::FaultFormation:
            faultFormation(commandBuffer);
            break;
        case DisplacementMethod::Noise:
            noiseHeightMap(commandBuffer);
            break;
        case DisplacementMethod::FFT:
            fftDisplacementMap(commandBuffer);
            break;
        case DisplacementMethod::Blend:
            blendDisplacementMap(commandBuffer);
            break;
        default:
            assert(false && "method not not yet implemented!");
    }
    bindlessDescriptor().update({ &m_displacementMap.values, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_info.values_tex_id });

    refreshDerivedMaps(commandBuffer);
}

Texture& DisplacementMapGenerator::displacementTexture() {
    return m_displacementMap.values;
}

Texture& DisplacementMapGenerator::normalTexture() {
    return m_displacementMap.normals;
}

Texture& DisplacementMapGenerator::slopeMoments0Texture() {
    return m_displacementMap.slopeMoments0;
}

Texture& DisplacementMapGenerator::slopeMoments1Texture() {
    return m_displacementMap.slopeMoments1;
}

void DisplacementMapGenerator::refreshDerivedMaps(VkCommandBuffer commandBuffer) {
    generateSlopeMomentMaps(commandBuffer);
    bindlessDescriptor().update({ &m_displacementMap.slopeMoments0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_info.slope_moments0_tex_id });
    bindlessDescriptor().update({ &m_displacementMap.slopeMoments1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_info.slope_moments1_tex_id });

    generateNormalMap(commandBuffer);
    bindlessDescriptor().update({ &m_displacementMap.normals, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_info.normal_tex_id });
}

bool DisplacementMapGenerator::regenerateIfNeeded(VkCommandBuffer commandBuffer) {
    if(!m_dirty) {
        return false;
    }

    m_dirty = false;
    if(m_method == DisplacementMethod::File) {
        if(!m_regenerateFile) {
            return false;
        }

        m_regenerateFile = false;
        loadDisplacementMap();
    }

    exec(commandBuffer);
    return true;
}

bool DisplacementMapGenerator::controls(bool show) {
    if(!show) {
        return false;
    }

    ImGui::Begin("Displacement");
    ImGui::SetWindowSize({0, 0});
    const bool dirty = controlsContent();
    ImGui::End();
    return dirty;
}

bool DisplacementMapGenerator::controlsContent() {
    bool dirty = false;
    static constexpr std::array<const char*, 6> methods{ "None", "File", "Fault formation", "Noise", "FFT", "Blend" };

    dirty |= stateFileControls();
    ImGui::Separator();

    int method = static_cast<int>(m_method);
    if(ImGui::Combo("Type", &method, methods.data(), static_cast<int>(methods.size()))) {
        m_method = static_cast<DisplacementMethod>(method);
        dirty = true;
    }

    if(m_method == DisplacementMethod::File || m_method == DisplacementMethod::None) {
        auto info = displacementMapInfo();
        ImGui::Text("%s: %u x %u", m_method == DisplacementMethod::File ? "File" : "Input", info.width, info.height);
    }else {
        int size[2] = { static_cast<int>(m_info.width), static_cast<int>(m_info.height) };
        if(ImGui::InputInt2("Size", size)) {
            m_info.width = static_cast<uint>(std::clamp(size[0], 16, 8192));
            m_info.height = static_cast<uint>(std::clamp(size[1], 16, 8192));
            m_displacementMap.width = m_info.width;
            m_displacementMap.height = m_info.height;
            dirty = true;
        }
    }

    if(m_method == DisplacementMethod::FaultFormation) {
        dirty |= ImGui::DragFloat2("Seed", &ff_options.seed.x, 1.0f);
        dirty |= sliderUint("Iterations", ff_options.maxIterations, 1, 10000);
        dirty |= ImGui::Checkbox("Blur", &ff_options.blur);
        if(ff_options.blur) {
            dirty |= ImGui::SliderInt("Blur iterations", &ff_options.blurIterations, 1, 64);
        }
    }else if(m_method == DisplacementMethod::Noise) {
        dirty |= ImGui::DragFloat2("Seed", &noise_constants.seed.x, 1.0f);
        dirty |= ImGui::DragFloat("Base frequency", &noise_constants.baseFrequency, 0.05f, 0.001f, 64.0f, "%.3f");
        dirty |= ImGui::DragFloat("Lacunarity", &noise_constants.lacunarity, 0.01f, 1.001f, 8.0f, "%.3f");
        dirty |= ImGui::SliderFloat("Gain", &noise_constants.gain, 0.0f, 1.0f);
        dirty |= sliderUint("Octaves", noise_constants.octaves, 1, 12);
        bool enableRidges = noise_constants.enableRidges == 1;
        if(ImGui::Checkbox("Ridges", &enableRidges)) {
            noise_constants.enableRidges = enableRidges ? 1u : 0u;
            dirty = true;
        }
    }else if(m_method == DisplacementMethod::FFT) {
        const auto fftSize = nextPowerOfTwo(std::max(m_info.width, m_info.height));
        ImGui::Text("FFT: %u x %u", fftSize, fftSize);
        dirty |= fftControls(fft_spectrum_constants, fftSize);
    }else if(m_method == DisplacementMethod::Blend) {
        dirty |= blendControls();
    }

    if(m_method != DisplacementMethod::File && ImGui::Button("Regenerate")) {
        dirty = true;
    }

    m_dirty |= dirty;
    return dirty;
}

bool DisplacementMapGenerator::stateFileControls() {
    bool dirty = false;

    ImGui::InputText("Generator JSON", m_stateFilePath.data(), m_stateFilePath.size());

    if(ImGui::Button("Browse")) {
        m_stateFileDialogOpen = true;
        m_stateFileDialogClosed = false;
    }
    ImGui::SameLine();

    if(ImGui::Button("Save")) {
        try {
            const auto path = jsonPath(m_stateFilePath.data());
            saveState(path);
            std::strncpy(m_stateFilePath.data(), path.string().c_str(), m_stateFilePath.size() - 1);
            m_stateFilePath.back() = '\0';
            setStateStatus(fmt::format("Saved {}", path.string()));
        }catch(const std::exception& err) {
            setStateStatus(err.what(), true);
        }
    }
    ImGui::SameLine();

    if(ImGui::Button("Load")) {
        try {
            const auto path = jsonPath(m_stateFilePath.data());
            loadState(path);
            std::strncpy(m_stateFilePath.data(), path.string().c_str(), m_stateFilePath.size() - 1);
            m_stateFilePath.back() = '\0';
            setStateStatus(fmt::format("Loaded {}", path.string()));
            dirty = true;
        }catch(const std::exception& err) {
            setStateStatus(err.what(), true);
        }
    }

    if(!m_stateStatus.empty()) {
        const auto color = m_stateStatusError ? ImVec4{1.0f, 0.25f, 0.25f, 1.0f} : ImVec4{0.45f, 0.85f, 0.45f, 1.0f};
        ImGui::TextColored(color, "%s", m_stateStatus.c_str());
    }

    openStateFileDialog();
    return dirty;
}

void DisplacementMapGenerator::openStateFileDialog() {
    if(!m_stateFileDialogOpen) {
        return;
    }

    FileDialog::extFilter = { "json" };
    FileDialog::ShowFileDialog(&m_stateFileDialogOpen, m_stateFilePath.data(), &m_stateFileDialogClosed, FileDialog::FileDialogType::OpenFile);
    if(m_stateFileDialogClosed) {
        m_stateFileDialogOpen = false;
        m_stateFileDialogClosed = false;
        FileDialog::extFilter.clear();
    }
}

void DisplacementMapGenerator::setStateStatus(std::string message, bool error) {
    m_stateStatus = std::move(message);
    m_stateStatusError = error;
}

void DisplacementMapGenerator::saveState(const fs::path& path) const {
    if(path.empty()) {
        throw std::runtime_error{"Select a JSON file path before saving"};
    }

    auto noiseJson = [](const NoiseConstants& constants) {
        return nlohmann::json{
            { "seed", toJson(constants.seed) },
            { "baseFrequency", constants.baseFrequency },
            { "lacunarity", constants.lacunarity },
            { "gain", constants.gain },
            { "octaves", constants.octaves },
            { "ridges", constants.enableRidges != 0u }
        };
    };

    auto fftJson = [](const FftSpectrumConstants& constants) {
        return nlohmann::json{
            { "seed", toJson(constants.seed) },
            { "amplitude", constants.amplitude },
            { "spectralPower", constants.spectralPower },
            { "frequencyCount", constants.frequencyCount },
            { "frequencies", constants.frequencies }
        };
    };

    nlohmann::json layers = nlohmann::json::array();
    for(const auto& layer : m_blendLayers) {
        layers.push_back({
            { "enabled", layer.enabled },
            { "source", enumName(static_cast<uint>(layer.source), BlendLayerSourceStateNames) },
            { "blendMode", enumName(static_cast<uint>(layer.blendMode), BlendModeStateNames) },
            { "opacity", layer.opacity },
            { "noise", noiseJson(layer.noise) },
            { "fft", fftJson(layer.fft) }
        });
    }

    const auto state = nlohmann::json{
        { "version", 1 },
        { "type", enumName(static_cast<uint>(m_method), DisplacementMethodStateNames) },
        { "parameters", {
            { "size", { m_info.width, m_info.height } },
            { "file", {
                { "path", m_path }
            } },
            { "faultFormation", {
                { "seed", toJson(ff_options.seed) },
                { "maxIterations", ff_options.maxIterations },
                { "blur", ff_options.blur },
                { "blurIterations", ff_options.blurIterations }
            } },
            { "noise", noiseJson(noise_constants) },
            { "fft", fftJson(fft_spectrum_constants) },
            { "blend", {
                { "layers", layers }
            } }
        } }
    };

    FileManager::save(state.dump(4), path, false);
}

void DisplacementMapGenerator::loadState(const fs::path& path) {
    if(path.empty()) {
        throw std::runtime_error{"Select a JSON file path before loading"};
    }

    const auto bytes = FileManager::instance().load(path.string(), false);
    const auto state = nlohmann::json::parse(std::string{bytes.begin(), bytes.end()});
    const auto& parameters = state.contains("parameters") && state["parameters"].is_object() ? state["parameters"] : state;

    const auto method = readEnumIndex(state, "type", DisplacementMethodStateNames, static_cast<uint>(m_method));
    m_method = static_cast<DisplacementMethod>(method);

    if(auto sizeItr = parameters.find("size"); sizeItr != parameters.end() && sizeItr->is_array() && sizeItr->size() >= 2) {
        m_info.width = std::clamp((*sizeItr)[0].get<uint>(), 16u, 8192u);
        m_info.height = std::clamp((*sizeItr)[1].get<uint>(), 16u, 8192u);
        m_displacementMap.width = m_info.width;
        m_displacementMap.height = m_info.height;
    }

    if(auto fileItr = parameters.find("file"); fileItr != parameters.end() && fileItr->is_object()) {
        auto pathItr = fileItr->find("path");
        if(pathItr != fileItr->end() && pathItr->is_string()) {
            const auto nextPath = pathItr->get<std::string>();
            m_regenerateFile |= nextPath != m_path;
            m_path = nextPath;
        }
    }

    if(auto faultItr = parameters.find("faultFormation"); faultItr != parameters.end() && faultItr->is_object()) {
        ff_options.seed = readVec2(*faultItr, "seed", ff_options.seed);
        ff_options.maxIterations = readUint(*faultItr, "maxIterations", ff_options.maxIterations);
        ff_options.blur = readBool(*faultItr, "blur", ff_options.blur);
        ff_options.blurIterations = readInt(*faultItr, "blurIterations", ff_options.blurIterations);
    }

    if(auto noiseItr = parameters.find("noise"); noiseItr != parameters.end() && noiseItr->is_object()) {
        noise_constants.seed = readVec2(*noiseItr, "seed", noise_constants.seed);
        noise_constants.baseFrequency = readFloat(*noiseItr, "baseFrequency", noise_constants.baseFrequency);
        noise_constants.lacunarity = readFloat(*noiseItr, "lacunarity", noise_constants.lacunarity);
        noise_constants.gain = readFloat(*noiseItr, "gain", noise_constants.gain);
        noise_constants.octaves = std::clamp(readUint(*noiseItr, "octaves", noise_constants.octaves), 1u, 12u);
        noise_constants.enableRidges = readBool(*noiseItr, "ridges", noise_constants.enableRidges != 0u) ? 1u : 0u;
    }

    if(auto fftItr = parameters.find("fft"); fftItr != parameters.end() && fftItr->is_object()) {
        fft_spectrum_constants.seed = readVec2(*fftItr, "seed", fft_spectrum_constants.seed);
        fft_spectrum_constants.amplitude = readFloat(*fftItr, "amplitude", fft_spectrum_constants.amplitude);
        fft_spectrum_constants.spectralPower = readFloat(*fftItr, "spectralPower", fft_spectrum_constants.spectralPower);
        fft_spectrum_constants.frequencyCount = std::clamp(readUint(*fftItr, "frequencyCount", fft_spectrum_constants.frequencyCount), 1u, 6u);
        fft_spectrum_constants.frequencies = readFrequencies(*fftItr, fft_spectrum_constants.frequencies);
        normalizeFftFrequencies(fft_spectrum_constants, nextPowerOfTwo(std::max(m_info.width, m_info.height)));
    }

    if(auto blendItr = parameters.find("blend"); blendItr != parameters.end() && blendItr->is_object()) {
        auto layersItr = blendItr->find("layers");
        if(layersItr != blendItr->end() && layersItr->is_array()) {
            m_blendLayers.clear();
            for(const auto& layerJson : *layersItr) {
                if(!layerJson.is_object()) {
                    continue;
                }

                BlendLayer layer{};
                layer.enabled = readBool(layerJson, "enabled", layer.enabled);
                layer.source = static_cast<BlendLayerSource>(readEnumIndex(layerJson, "source", BlendLayerSourceStateNames, static_cast<uint>(layer.source)));
                layer.blendMode = static_cast<BlendMode>(readEnumIndex(layerJson, "blendMode", BlendModeStateNames, static_cast<uint>(layer.blendMode)));
                layer.opacity = std::clamp(readFloat(layerJson, "opacity", layer.opacity), 0.0f, 1.0f);

                if(auto layerNoiseItr = layerJson.find("noise"); layerNoiseItr != layerJson.end() && layerNoiseItr->is_object()) {
                    layer.noise.seed = readVec2(*layerNoiseItr, "seed", layer.noise.seed);
                    layer.noise.baseFrequency = readFloat(*layerNoiseItr, "baseFrequency", layer.noise.baseFrequency);
                    layer.noise.lacunarity = readFloat(*layerNoiseItr, "lacunarity", layer.noise.lacunarity);
                    layer.noise.gain = readFloat(*layerNoiseItr, "gain", layer.noise.gain);
                    layer.noise.octaves = std::clamp(readUint(*layerNoiseItr, "octaves", layer.noise.octaves), 1u, 12u);
                    layer.noise.enableRidges = readBool(*layerNoiseItr, "ridges", layer.noise.enableRidges != 0u) ? 1u : 0u;
                }

                if(auto layerFftItr = layerJson.find("fft"); layerFftItr != layerJson.end() && layerFftItr->is_object()) {
                    layer.fft.seed = readVec2(*layerFftItr, "seed", layer.fft.seed);
                    layer.fft.amplitude = readFloat(*layerFftItr, "amplitude", layer.fft.amplitude);
                    layer.fft.spectralPower = readFloat(*layerFftItr, "spectralPower", layer.fft.spectralPower);
                    layer.fft.frequencyCount = std::clamp(readUint(*layerFftItr, "frequencyCount", layer.fft.frequencyCount), 1u, 6u);
                    layer.fft.frequencies = readFrequencies(*layerFftItr, layer.fft.frequencies);
                    normalizeFftFrequencies(layer.fft, nextPowerOfTwo(std::max(m_info.width, m_info.height)));
                }

                m_blendLayers.push_back(layer);
            }

            if(m_blendLayers.empty()) {
                m_blendLayers.emplace_back();
            }
        }
    }

    m_dirty = true;
    m_regenerateFile |= m_method == DisplacementMethod::File;
}

bool DisplacementMapGenerator::blendControls() {
    bool dirty = false;

    if(ImGui::Button("Add layer")) {
        auto layer = m_blendLayers.empty() ? BlendLayer{} : m_blendLayers.back();
        layer.noise.seed += glm::vec2{137.0f, 263.0f};
        layer.fft.seed += glm::vec2{211.0f, 97.0f};
        layer.blendMode = BlendMode::Overlay;
        layer.opacity = 0.5f;
        m_blendLayers.push_back(layer);
        dirty = true;
    }
    ImGui::SameLine();
    if(ImGui::Button("Remove layer") && m_blendLayers.size() > 1) {
        m_blendLayers.pop_back();
        dirty = true;
    }

    const auto fftSize = nextPowerOfTwo(std::max(m_info.width, m_info.height));
    ImGui::Text("Layers: %zu   FFT: %u x %u", m_blendLayers.size(), fftSize, fftSize);

    for(int i = static_cast<int>(m_blendLayers.size()) - 1; i >= 0; --i) {
        dirty |= blendLayerControls(m_blendLayers[i], i);
    }

    return dirty;
}

bool DisplacementMapGenerator::blendLayerControls(BlendLayer& layer, int layerIndex) {
    bool dirty = false;

    ImGui::PushID(layerIndex);
    ImGui::Separator();
    ImGui::Text("Layer %d%s", layerIndex, layerIndex == 0 ? " (bottom)" : "");

    dirty |= ImGui::Checkbox("Enabled", &layer.enabled);

    int source = static_cast<int>(layer.source);
    if(ImGui::Combo("Source", &source, BlendLayerSources.data(), static_cast<int>(BlendLayerSources.size()))) {
        layer.source = static_cast<BlendLayerSource>(source);
        dirty = true;
    }

    if(layerIndex > 0) {
        int blendMode = static_cast<int>(layer.blendMode);
        if(ImGui::Combo("Blend mode", &blendMode, BlendModes.data(), static_cast<int>(BlendModes.size()))) {
            layer.blendMode = static_cast<BlendMode>(blendMode);
            dirty = true;
        }
        dirty |= ImGui::SliderFloat("Opacity", &layer.opacity, 0.0f, 1.0f);
    }

    if(layer.source == BlendLayerSource::Noise) {
        dirty |= ImGui::DragFloat2("Seed", &layer.noise.seed.x, 1.0f);
        dirty |= ImGui::DragFloat("Base frequency", &layer.noise.baseFrequency, 0.05f, 0.001f, 64.0f, "%.3f");
        dirty |= ImGui::DragFloat("Lacunarity", &layer.noise.lacunarity, 0.01f, 1.001f, 8.0f, "%.3f");
        dirty |= ImGui::SliderFloat("Gain", &layer.noise.gain, 0.0f, 1.0f);
        dirty |= sliderUint("Octaves", layer.noise.octaves, 1, 12);
        bool enableRidges = layer.noise.enableRidges == 1;
        if(ImGui::Checkbox("Ridges", &enableRidges)) {
            layer.noise.enableRidges = enableRidges ? 1u : 0u;
            dirty = true;
        }
    }else {
        const auto fftSize = nextPowerOfTwo(std::max(m_info.width, m_info.height));
        dirty |= fftControls(layer.fft, fftSize);
    }

    ImGui::PopID();
    return dirty;
}

bool DisplacementMapGenerator::fftControls(FftSpectrumConstants& constants, uint fftSize) {
    bool dirty = false;
    normalizeFftFrequencies(constants, fftSize);

    dirty |= ImGui::DragFloat2("Seed", &constants.seed.x, 1.0f);
    dirty |= ImGui::DragFloat("Amplitude", &constants.amplitude, 0.005f, 0.0f, 4.0f, "%.3f");
    dirty |= ImGui::DragFloat("Spectral falloff", &constants.spectralPower, 0.02f, 0.25f, 6.0f, "%.2f");

    int frequencyCount = static_cast<int>(constants.frequencyCount);
    if(ImGui::SliderInt("Frequency count", &frequencyCount, 1, 6)) {
        constants.frequencyCount = static_cast<uint>(std::clamp(frequencyCount, 1, 6));
        dirty = true;
    }

    const float maxFrequency = std::max(float(fftSize) * 0.5f, 0.001f);
    for(int i = 0; i < static_cast<int>(constants.frequencyCount); ++i) {
        ImGui::PushID(i);
        ImGui::Text("Frequency %d", i + 1);
        ImGui::SameLine();
        dirty |= ImGui::DragFloat("##frequency", &constants.frequencies[i], 0.25f, 0.001f, maxFrequency, "%.2f");
        ImGui::PopID();
    }

    if(dirty) {
        normalizeFftFrequencies(constants, fftSize);
    }

    return dirty;
}

void DisplacementMapGenerator::normalizeFftFrequencies(FftSpectrumConstants& constants, uint fftSize) {
    constants.frequencyCount = std::clamp(constants.frequencyCount, 1u, 6u);
    const float maxFrequency = std::max(float(fftSize) * 0.5f, 0.001f);
    for(auto& frequency : constants.frequencies) {
        frequency = std::clamp(frequency, 0.001f, maxFrequency);
    }

    std::sort(constants.frequencies.begin(), constants.frequencies.begin() + constants.frequencyCount);
}

void DisplacementMapGenerator::loadDisplacementMap() {
    if(m_path.empty()) return;

    stbi_set_flip_vertically_on_load(0);
    auto pixels = stbi_load(m_path.c_str(), &m_fileInfo.width, &m_fileInfo.height, &m_fileInfo.channels, STBI_rgb_alpha);
    if(!pixels){
        throw std::runtime_error{fmt::format("failed to load texture image {}!", m_path)};
    }
    VkDeviceSize size = m_fileInfo.width * m_fileInfo.height * STBI_rgb_alpha;
    m_fileInfo.pixels = device().createDeviceLocalBuffer(pixels, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    stbi_image_free(pixels);
}

void DisplacementMapGenerator::createComputePipelines() {
    m_compute = ComputePipelines(&device(), metadata());
    m_compute.createPipelines();
}

void DisplacementMapGenerator::computeFileDisplacementMap(VkCommandBuffer commandBuffer) {
    VkPipelineStageFlags2 srcStageMask = GeneratedTextureReadStages;
    VkAccessFlags2 srcAccessMask = GeneratedTextureReadAccess;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if( m_displacementMap.values.format != VK_FORMAT_R8G8B8A8_UNORM) {
        srcStageMask = VK_PIPELINE_STAGE_NONE;
        srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        srcAccessMask = VK_ACCESS_NONE;
        textures::createNoTransition(device(), m_displacementMap.values, VK_IMAGE_TYPE_2D,
                                     VK_FORMAT_R8G8B8A8_UNORM, {m_fileInfo.width, m_fileInfo.height, 1},
                                     DepthMapAddressMode);
    }

    VkBufferImageCopy2 region{ VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2 };
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { to<uint>(m_fileInfo.width), to<uint>(m_fileInfo.height), 1 };

    VkCopyBufferToImageInfo2 copyInfo{ VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2 };
    copyInfo.srcBuffer = m_fileInfo.pixels;
    copyInfo.dstImage = m_displacementMap.values.image;
    copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    copyInfo.regionCount = 1;
    copyInfo.pRegions = &region;

    Barriers::pushAndFlush(commandBuffer, m_displacementMap.values.image, DEFAULT_SUB_RANGE, srcStageMask,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, srcAccessMask, VK_ACCESS_TRANSFER_WRITE_BIT,
                           srcLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    vkCmdCopyBufferToImage2(commandBuffer, &copyInfo);

    Barriers::pushAndFlush(commandBuffer, m_displacementMap.values.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

}

void DisplacementMapGenerator::noneDisplacementMap(VkCommandBuffer commandBuffer) {
    auto info = displacementMapInfo();
    auto& dispMap = m_displacementMap.values;

    VkPipelineStageFlags2 srcStageMask = GeneratedTextureReadStages;
    VkAccessFlags2 srcAccessMask = GeneratedTextureReadAccess;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if(dispMap.format != VK_FORMAT_R16_SFLOAT || dispMap.width != info.width || dispMap.height != info.height) {
        srcStageMask = VK_PIPELINE_STAGE_NONE;
        srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        srcAccessMask = VK_ACCESS_NONE;
        textures::createNoTransition(device(), dispMap, VK_IMAGE_TYPE_2D,
                                     VK_FORMAT_R16_SFLOAT, {info.width, info.height, 1},
                                     DepthMapAddressMode);
    }

    Barriers::pushAndFlush(commandBuffer, dispMap.image, DEFAULT_SUB_RANGE, srcStageMask, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           srcAccessMask, VK_ACCESS_TRANSFER_WRITE_BIT, srcLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkClearColorValue clearColor{};
    vkCmdClearColorImage(commandBuffer, dispMap.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &DEFAULT_SUB_RANGE);

    Barriers::pushAndFlush(commandBuffer, dispMap.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    dispMap.image.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void DisplacementMapGenerator::faultFormation(VkCommandBuffer commandBuffer) {
    auto info = displacementMapInfo();
    auto& dispMap = m_displacementMap.values;

    textures::createNoTransition(device(), m_displacementMap.values, VK_IMAGE_TYPE_2D,
                                 VK_FORMAT_R16_SFLOAT, {info.width, info.height, 1},
                                 DepthMapAddressMode);

    if(m_faultFormationImageId == ~0u) {
        m_faultFormationImageId = bindlessDescriptor().reserveImageSlots(1);
    }
    bindlessDescriptor().update({ &dispMap, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_faultFormationImageId, VK_IMAGE_LAYOUT_GENERAL });

    Barriers::pushAndFlush(commandBuffer, dispMap.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    const auto gx = (info.width + 15)/16;
    const auto gy = (info.height + 15)/16;

    auto descriptorSet = bindlessDescriptorSet();
    ff_constants.seed = ff_options.seed;
    ff_constants.maxIterations = ff_options.maxIterations;
    const auto N = ff_constants.maxIterations;
    ff_constants.dmap_image_index = m_faultFormationImageId;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("fault_formation"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("fault_formation"), 0, 1, &descriptorSet, 0, 0);

    for(int i = 0; i <= N; ++i) {
        ff_constants.iteration = i;
        vkCmdPushConstants(commandBuffer, m_compute.layout("fault_formation"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ff_constants), &ff_constants);
        vkCmdDispatch(commandBuffer, gx, gy, 1);

        Barrier::computeWriteToRead(commandBuffer);
    }

    Barriers::pushAndFlush(commandBuffer, dispMap.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    dispMap.image.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if(ff_options.blur) {
        blur(commandBuffer);
    }

}

void DisplacementMapGenerator::noiseHeightMap(VkCommandBuffer commandBuffer) {
    auto info = displacementMapInfo();
    auto& dispMap = m_displacementMap.values;

    VkPipelineStageFlags2 srcStageMask = GeneratedTextureReadStages;
    VkAccessFlags2 srcAccessMask = GeneratedTextureReadAccess;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if(dispMap.format != VK_FORMAT_R16_SFLOAT || dispMap.width != info.width || dispMap.height != info.height) {
        srcStageMask = VK_PIPELINE_STAGE_NONE;
        srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        srcAccessMask = VK_ACCESS_NONE;
        textures::createNoTransition(device(), dispMap, VK_IMAGE_TYPE_2D,
                                     VK_FORMAT_R16_SFLOAT, {info.width, info.height, 1},
                                     DepthMapAddressMode);
    }

    if(m_noiseImageId == ~0u) {
        m_noiseImageId = bindlessDescriptor().reserveImageSlots(1);
    }

    bindlessDescriptor().update({ &dispMap, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_noiseImageId, VK_IMAGE_LAYOUT_GENERAL });

    Barriers::pushAndFlush(commandBuffer, dispMap.image, DEFAULT_SUB_RANGE, srcStageMask, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           srcAccessMask, VK_ACCESS_SHADER_WRITE_BIT, srcLayout, VK_IMAGE_LAYOUT_GENERAL);

    noise_constants.dmap_image_index = m_noiseImageId;
    const auto gx = (info.width + 31)/32;
    const auto gy = (info.height + 31)/32;

    auto descriptorSet = bindlessDescriptorSet();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("noise_height_map_gen"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("noise_height_map_gen"), 0, 1, &descriptorSet, 0, 0);
    vkCmdPushConstants(commandBuffer, m_compute.layout("noise_height_map_gen"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(noise_constants), &noise_constants);
    vkCmdDispatch(commandBuffer, gx, gy, 1);

    Barriers::pushAndFlush(commandBuffer, dispMap.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                           VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    dispMap.image.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void DisplacementMapGenerator::createFftTextures(VkCommandBuffer commandBuffer, uint fftSize) {
    bool queuedBarriers = false;
    auto createSignalTexture = [&](Texture& texture) {
        const bool recreate = texture.format != VK_FORMAT_R32G32_SFLOAT || texture.width != fftSize || texture.height != fftSize;
        if(!recreate) {
            return;
        }

        textures::createNoTransition(device(), texture, VK_IMAGE_TYPE_2D,
                                     VK_FORMAT_R32G32_SFLOAT, {fftSize, fftSize, 1},
                                     VK_SAMPLER_ADDRESS_MODE_REPEAT);

        Barriers::push(texture.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        texture.image.currentLayout = VK_IMAGE_LAYOUT_GENERAL;
        queuedBarriers = true;
    };

    createSignalTexture(m_fftPing);
    createSignalTexture(m_fftPong);
    if(queuedBarriers) {
        Barriers::flush(commandBuffer);
    }
}

void DisplacementMapGenerator::fftDisplacementMap(VkCommandBuffer commandBuffer) {
    auto info = displacementMapInfo();
    const auto fftSize = nextPowerOfTwo(std::max(info.width, info.height));
    createFftTextures(commandBuffer, fftSize);

    auto& dispMap = m_displacementMap.values;
    VkPipelineStageFlags2 srcStageMask = GeneratedTextureReadStages;
    VkAccessFlags2 srcAccessMask = GeneratedTextureReadAccess;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if(dispMap.format != VK_FORMAT_R16_SFLOAT || dispMap.width != info.width || dispMap.height != info.height) {
        srcStageMask = VK_PIPELINE_STAGE_NONE;
        srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        srcAccessMask = VK_ACCESS_NONE;
        textures::createNoTransition(device(), dispMap, VK_IMAGE_TYPE_2D,
                                     VK_FORMAT_R16_SFLOAT, {info.width, info.height, 1},
                                     DepthMapAddressMode);
    }

    if(m_fftTextureOffset == ~0u) {
        m_fftTextureOffset = bindlessDescriptor().reserveTextureSlots(2);
    }
    if(m_fftImageOffset == ~0u) {
        m_fftImageOffset = bindlessDescriptor().reserveImageSlots(2);
    }
    if(m_fftDisplacementImageId == ~0u) {
        m_fftDisplacementImageId = bindlessDescriptor().reserveImageSlots(1);
    }

    const std::array<uint, 2> fftTextureIds{m_fftTextureOffset, m_fftTextureOffset + 1};
    const std::array<uint, 2> fftImageIds{m_fftImageOffset, m_fftImageOffset + 1};

    bindlessDescriptor().update({ &m_fftPing, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, fftTextureIds[0], VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &m_fftPong, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, fftTextureIds[1], VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &m_fftPing, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, fftImageIds[0], VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &m_fftPong, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, fftImageIds[1], VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &dispMap, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_fftDisplacementImageId, VK_IMAGE_LAYOUT_GENERAL });

    Barriers::pushAndFlush(commandBuffer, dispMap.image, DEFAULT_SUB_RANGE, srcStageMask, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           srcAccessMask, VK_ACCESS_SHADER_WRITE_BIT, srcLayout, VK_IMAGE_LAYOUT_GENERAL);

    auto descriptorSet = bindlessDescriptorSet();
    const auto gx = (fftSize + 31) / 32;
    const auto gy = (fftSize + 31) / 32;

    normalizeFftFrequencies(fft_spectrum_constants, fftSize);
    fft_spectrum_constants.output_image_index = fftImageIds[0];
    fft_spectrum_constants.size = fftSize;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("fft_spectrum"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("fft_spectrum"), 0, 1, &descriptorSet, 0, 0);
    vkCmdPushConstants(commandBuffer, m_compute.layout("fft_spectrum"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(fft_spectrum_constants), &fft_spectrum_constants);
    vkCmdDispatch(commandBuffer, gx, gy, 1);
    Barrier::computeWriteToRead(commandBuffer);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("fft_reorder"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("fft_reorder"), 0, 1, &descriptorSet, 0, 0);

    int readIndex = 0;
    int writeIndex = 1;

    fft_reorder_constants = {
        .input_tex_id = fftTextureIds[readIndex],
        .output_image_index = fftImageIds[writeIndex],
        .size = fftSize,
        .horizontal = 1
    };
    vkCmdPushConstants(commandBuffer, m_compute.layout("fft_reorder"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(fft_reorder_constants), &fft_reorder_constants);
    vkCmdDispatch(commandBuffer, gx, gy, 1);
    Barrier::computeWriteToRead(commandBuffer);
    std::swap(readIndex, writeIndex);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("fft_pass"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("fft_pass"), 0, 1, &descriptorSet, 0, 0);

    const auto numPasses = log2PowerOfTwo(fftSize);
    for(uint pass = 0; pass < numPasses; ++pass) {
        fft_pass_constants = {
            .input_tex_id = fftTextureIds[readIndex],
            .output_image_index = fftImageIds[writeIndex],
            .size = fftSize,
            .pass = pass,
            .horizontal = 1
        };
        vkCmdPushConstants(commandBuffer, m_compute.layout("fft_pass"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(fft_pass_constants), &fft_pass_constants);
        vkCmdDispatch(commandBuffer, gx, gy, 1);
        Barrier::computeWriteToRead(commandBuffer);
        std::swap(readIndex, writeIndex);
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("fft_reorder"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("fft_reorder"), 0, 1, &descriptorSet, 0, 0);
    fft_reorder_constants = {
        .input_tex_id = fftTextureIds[readIndex],
        .output_image_index = fftImageIds[writeIndex],
        .size = fftSize,
        .horizontal = 0
    };
    vkCmdPushConstants(commandBuffer, m_compute.layout("fft_reorder"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(fft_reorder_constants), &fft_reorder_constants);
    vkCmdDispatch(commandBuffer, gx, gy, 1);
    Barrier::computeWriteToRead(commandBuffer);
    std::swap(readIndex, writeIndex);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("fft_pass"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("fft_pass"), 0, 1, &descriptorSet, 0, 0);
    for(uint pass = 0; pass < numPasses; ++pass) {
        fft_pass_constants = {
            .input_tex_id = fftTextureIds[readIndex],
            .output_image_index = fftImageIds[writeIndex],
            .size = fftSize,
            .pass = pass,
            .horizontal = 0
        };
        vkCmdPushConstants(commandBuffer, m_compute.layout("fft_pass"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(fft_pass_constants), &fft_pass_constants);
        vkCmdDispatch(commandBuffer, gx, gy, 1);
        Barrier::computeWriteToRead(commandBuffer);
        std::swap(readIndex, writeIndex);
    }

    fft_displacement_constants = {
        .input_tex_id = fftTextureIds[readIndex],
        .dmap_image_index = m_fftDisplacementImageId,
        .fftSize = fftSize,
        ._padding = 0
    };

    const auto dmapGx = (info.width + 31) / 32;
    const auto dmapGy = (info.height + 31) / 32;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("fft_to_displacement"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("fft_to_displacement"), 0, 1, &descriptorSet, 0, 0);
    vkCmdPushConstants(commandBuffer, m_compute.layout("fft_to_displacement"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(fft_displacement_constants), &fft_displacement_constants);
    vkCmdDispatch(commandBuffer, dmapGx, dmapGy, 1);

    Barriers::pushAndFlush(commandBuffer, dispMap.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                           VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    dispMap.image.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void DisplacementMapGenerator::createBlendTextures(VkCommandBuffer commandBuffer) {
    auto info = displacementMapInfo();
    bool queuedBarriers = false;

    auto createBlendTexture = [&](Texture& texture) {
        const bool recreate = texture.format != VK_FORMAT_R16_SFLOAT || texture.width != info.width || texture.height != info.height;
        if(!recreate) {
            return;
        }

        textures::createNoTransition(device(), texture, VK_IMAGE_TYPE_2D,
                                     VK_FORMAT_R16_SFLOAT, {info.width, info.height, 1},
                                     DepthMapAddressMode);

        Barriers::push(texture.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        texture.image.currentLayout = VK_IMAGE_LAYOUT_GENERAL;
        queuedBarriers = true;
    };

    createBlendTexture(m_blendLayer);
    createBlendTexture(m_blendAccumulator[0]);
    createBlendTexture(m_blendAccumulator[1]);

    if(queuedBarriers) {
        Barriers::flush(commandBuffer);
    }
}

void DisplacementMapGenerator::generateBlendLayer(VkCommandBuffer commandBuffer, BlendLayer& layer) {
    if(layer.source == BlendLayerSource::Noise) {
        auto previousNoiseConstants = noise_constants;
        noise_constants = layer.noise;
        noiseHeightMap(commandBuffer);
        noise_constants = previousNoiseConstants;
    }else {
        auto previousFftConstants = fft_spectrum_constants;
        fft_spectrum_constants = layer.fft;
        fftDisplacementMap(commandBuffer);
        fft_spectrum_constants = previousFftConstants;
    }

    textures::copy(commandBuffer, m_displacementMap.values, m_blendLayer);
}

void DisplacementMapGenerator::dispatchBlend(VkCommandBuffer commandBuffer, uint baseTextureId, uint layerTextureId,
                                             uint outputImageId, BlendMode blendMode, float opacity, float dissolveSeed) {
    auto info = displacementMapInfo();
    blend_constants = {
        .base_tex_id = baseTextureId,
        .layer_tex_id = layerTextureId,
        .output_image_index = outputImageId,
        .blendMode = static_cast<uint>(blendMode),
        .opacity = std::clamp(opacity, 0.0f, 1.0f),
        .dissolveSeed = dissolveSeed,
        ._padding = {}
    };

    auto descriptorSet = bindlessDescriptorSet();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("blend_height_maps"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("blend_height_maps"), 0, 1, &descriptorSet, 0, 0);
    vkCmdPushConstants(commandBuffer, m_compute.layout("blend_height_maps"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(blend_constants), &blend_constants);
    vkCmdDispatch(commandBuffer, (info.width + 31) / 32, (info.height + 31) / 32, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void DisplacementMapGenerator::blendDisplacementMap(VkCommandBuffer commandBuffer) {
    if(m_blendLayers.empty()) {
        m_blendLayers.emplace_back();
    }

    createBlendTextures(commandBuffer);

    if(m_blendTextureOffset == ~0u) {
        m_blendTextureOffset = bindlessDescriptor().reserveTextureSlots(3);
    }
    if(m_blendImageOffset == ~0u) {
        m_blendImageOffset = bindlessDescriptor().reserveImageSlots(3);
    }
    if(m_blendDisplacementImageId == ~0u) {
        m_blendDisplacementImageId = bindlessDescriptor().reserveImageSlots(1);
    }

    const std::array<uint, 3> textureIds{m_blendTextureOffset, m_blendTextureOffset + 1, m_blendTextureOffset + 2};
    const std::array<uint, 3> imageIds{m_blendImageOffset, m_blendImageOffset + 1, m_blendImageOffset + 2};

    bindlessDescriptor().update({ &m_blendLayer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureIds[0], VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &m_blendAccumulator[0], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureIds[1], VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &m_blendAccumulator[1], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureIds[2], VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &m_blendLayer, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, imageIds[0], VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &m_blendAccumulator[0], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, imageIds[1], VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &m_blendAccumulator[1], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, imageIds[2], VK_IMAGE_LAYOUT_GENERAL });

    bool initialized = false;
    int readIndex = 0;
    int writeIndex = 1;

    for(auto layerIndex = 0; layerIndex < static_cast<int>(m_blendLayers.size()); ++layerIndex) {
        auto& layer = m_blendLayers[layerIndex];
        if(!layer.enabled) {
            continue;
        }

        generateBlendLayer(commandBuffer, layer);
        bindlessDescriptor().update({ &m_blendLayer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureIds[0], VK_IMAGE_LAYOUT_GENERAL });

        if(!initialized) {
            dispatchBlend(commandBuffer, textureIds[0], textureIds[0], imageIds[readIndex + 1],
                          BlendMode::Normal, 1.0f, float(layerIndex));
            initialized = true;
        }else {
            dispatchBlend(commandBuffer, textureIds[readIndex + 1], textureIds[0], imageIds[writeIndex + 1],
                          layer.blendMode, layer.opacity, float(layerIndex));
            std::swap(readIndex, writeIndex);
        }
    }

    if(!initialized) {
        noneDisplacementMap(commandBuffer);
        return;
    }

    auto info = displacementMapInfo();
    auto& dispMap = m_displacementMap.values;
    bool finalRecreated = false;
    if(dispMap.format != VK_FORMAT_R16_SFLOAT || dispMap.width != info.width || dispMap.height != info.height) {
        textures::createNoTransition(device(), dispMap, VK_IMAGE_TYPE_2D,
                                     VK_FORMAT_R16_SFLOAT, {info.width, info.height, 1},
                                     DepthMapAddressMode);
        finalRecreated = true;
    }

    const auto oldLayout = finalRecreated || dispMap.image.currentLayout == VK_IMAGE_LAYOUT_UNDEFINED
        ? VK_IMAGE_LAYOUT_UNDEFINED
        : dispMap.image.currentLayout;
    const auto srcStage = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_NONE : GeneratedTextureReadStages;
    const auto srcAccess = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_ACCESS_NONE : GeneratedTextureReadAccess;

    bindlessDescriptor().update({ &dispMap, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_blendDisplacementImageId, VK_IMAGE_LAYOUT_GENERAL });

    Barriers::pushAndFlush(commandBuffer, dispMap.image, DEFAULT_SUB_RANGE, srcStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           srcAccess, VK_ACCESS_SHADER_WRITE_BIT, oldLayout, VK_IMAGE_LAYOUT_GENERAL);

    dispatchBlend(commandBuffer, textureIds[readIndex + 1], textureIds[readIndex + 1],
                  m_blendDisplacementImageId, BlendMode::Normal, 1.0f, 0.0f);

    Barriers::pushAndFlush(commandBuffer, dispMap.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                           VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    dispMap.image.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

std::vector<PipelineMetaData> DisplacementMapGenerator::metadata() {
    return {
            {
                .name = "generate_normals",
                .shadePath = FileManager::resource("vista_generate_normal_map.comp.spv"),
                .layouts = { &bindlessDescriptorSetLayout() },
                .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NormalGenConstants)} }
            },
            {
                .name = "generate_slope_moments",
                .shadePath = FileManager::resource("vista_slope_moments.comp.spv"),
                .layouts = { &bindlessDescriptorSetLayout() },
                .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SlopeMomentConstants)} }
            },
            {
                .name = "fault_formation",
                .shadePath = FileManager::resource("vista_fault_formation.comp.spv"),
                .layouts = { &bindlessDescriptorSetLayout() },
                .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ff_constants)} }
            },
            {
                .name = "noise_height_map_gen",
                .shadePath = FileManager::resource("vista_noise_height_map_gen.comp.spv"),
                .layouts = { &bindlessDescriptorSetLayout() },
                .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NoiseConstants)} }
            },
            {
                .name = "fft_spectrum",
                .shadePath = FileManager::resource("vista_fft_spectrum.comp.spv"),
                .layouts = { &bindlessDescriptorSetLayout() },
                .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FftSpectrumConstants)} }
            },
            {
                .name = "fft_reorder",
                .shadePath = FileManager::resource("vista_fft_reorder.comp.spv"),
                .layouts = { &bindlessDescriptorSetLayout() },
                .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FftReorderConstants)} }
            },
            {
                .name = "fft_pass",
                .shadePath = FileManager::resource("vista_fft_pass.comp.spv"),
                .layouts = { &bindlessDescriptorSetLayout() },
                .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FftPassConstants)} }
            },
            {
                .name = "fft_to_displacement",
                .shadePath = FileManager::resource("vista_fft_to_displacement.comp.spv"),
                .layouts = { &bindlessDescriptorSetLayout() },
                .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(FftDisplacementConstants)} }
            },
            {
                .name = "blend_height_maps",
                .shadePath = FileManager::resource("vista_blend_height_maps.comp.spv"),
                .layouts = { &bindlessDescriptorSetLayout() },
                .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BlendConstants)} }
            },
            {
                .name = "blur",
                .shadePath = FileManager::resource("vista_blur.comp.spv"),
                .layouts = { &bindlessDescriptorSetLayout() },
                .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint) * 3} }
            },
    };
}

VulkanDevice &DisplacementMapGenerator::device() {
    return *m_context->device;
}

DisplacementMapInfo DisplacementMapGenerator::displacementMapInfo() const {
    auto rtVal = m_info;
    if((m_method == DisplacementMethod::File || m_method == DisplacementMethod::None) && m_fileInfo.width > 0 && m_fileInfo.height > 0) {
        rtVal.width = to<uint>(m_fileInfo.width);
        rtVal.height = to<uint>(m_fileInfo.height);
    }
    return rtVal;
}

void DisplacementMapGenerator::setTerrainMetrics(glm::vec2 terrainWorldSize, glm::vec2 heightScale) {
    const float heightRange = std::abs(heightScale.y - heightScale.x);
    m_derivedMapHeightScale = {
        heightRange / std::max(std::abs(terrainWorldSize.x), 0.000001f),
        heightRange / std::max(std::abs(terrainWorldSize.y), 0.000001f)
    };
}

void DisplacementMapGenerator::generateNormalMap(VkCommandBuffer commandBuffer) {
    auto info = displacementMapInfo();
    auto& normalMap = m_displacementMap.normals;

    VkPipelineStageFlags2 srcStageMask = GeneratedTextureReadStages;
    VkAccessFlags2 srcAccessMask = GeneratedTextureReadAccess;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    const auto levels = to<uint>(std::log2(std::max(info.width, info.height))) + 1u;
    if(normalMap.width != info.width || normalMap.height != info.height) {
        srcStageMask = VK_PIPELINE_STAGE_NONE;
        srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        srcAccessMask = VK_ACCESS_NONE;
        m_displacementMap.normals.levels = levels;
        textures::createNoTransition(device(), m_displacementMap.normals, VK_IMAGE_TYPE_2D,
                                     VK_FORMAT_R16G16B16A16_SFLOAT, {info.width, info.height, 1});
    }

    static auto normalMapImageId = bindlessDescriptor().update(normalMap, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);


    auto subresource = DEFAULT_SUB_RANGE;
    subresource.levelCount = levels;
    Barriers::pushAndFlush(commandBuffer, normalMap.image, subresource, srcStageMask, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           srcAccessMask, VK_ACCESS_SHADER_WRITE_BIT, srcLayout, VK_IMAGE_LAYOUT_GENERAL);

    const auto gx = (info.width + 15)/16;
    const auto gy = (info.height + 15)/16;

    NormalGenConstants constants {
        .bump_strength = 1000.0f,
        .sigma = 1.5f,
        .sampleRadius = 4,
        .heightScaleX = m_derivedMapHeightScale.x,
        .heightScaleY = m_derivedMapHeightScale.y,
        .dmap_tex_id = info.values_tex_id,
        .normal_image_id = normalMapImageId
    };

    auto descriptorSet = bindlessDescriptorSet();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("generate_normals"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("generate_normals"), 0, 1, &descriptorSet, 0, 0);
    vkCmdPushConstants(commandBuffer, m_compute.layout("generate_normals"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, gx, gy, 1);

    Barriers::pushAndFlush(commandBuffer, normalMap.image, subresource, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    textures::generateLOD(commandBuffer, m_displacementMap.normals.image, info.width, info.height, levels);
}

void DisplacementMapGenerator::generateSlopeMomentMaps(VkCommandBuffer commandBuffer) {
    auto info = displacementMapInfo();
    auto& moments0 = m_displacementMap.slopeMoments0;
    auto& moments1 = m_displacementMap.slopeMoments1;

    VkPipelineStageFlags2 srcStageMask = GeneratedTextureReadStages;
    VkAccessFlags2 srcAccessMask = GeneratedTextureReadAccess;
    VkImageLayout srcLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    const auto levels = to<uint>(std::log2(std::max(info.width, info.height))) + 1u;
    const bool recreate = moments0.width != info.width || moments0.height != info.height
        || moments1.width != info.width || moments1.height != info.height;
    if(recreate) {
        srcStageMask = VK_PIPELINE_STAGE_NONE;
        srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        srcAccessMask = VK_ACCESS_NONE;
        moments0.levels = levels;
        moments1.levels = levels;
        textures::createNoTransition(device(), moments0, VK_IMAGE_TYPE_2D,
                                     VK_FORMAT_R32G32B32A32_SFLOAT, {info.width, info.height, 1});
        textures::createNoTransition(device(), moments1, VK_IMAGE_TYPE_2D,
                                     VK_FORMAT_R32G32B32A32_SFLOAT, {info.width, info.height, 1});
    }

    if(m_slopeMoments0ImageId == ~0u) {
        m_slopeMoments0ImageId = bindlessDescriptor().reserveImageSlots(1);
    }
    if(m_slopeMoments1ImageId == ~0u) {
        m_slopeMoments1ImageId = bindlessDescriptor().reserveImageSlots(1);
    }

    bindlessDescriptor().update({ &moments0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_slopeMoments0ImageId, VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &moments1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, m_slopeMoments1ImageId, VK_IMAGE_LAYOUT_GENERAL });

    auto subresource = DEFAULT_SUB_RANGE;
    subresource.levelCount = levels;
    Barriers::push(moments0.image, subresource, srcStageMask, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   srcAccessMask, VK_ACCESS_SHADER_WRITE_BIT, srcLayout, VK_IMAGE_LAYOUT_GENERAL);
    Barriers::push(moments1.image, subresource, srcStageMask, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   srcAccessMask, VK_ACCESS_SHADER_WRITE_BIT, srcLayout, VK_IMAGE_LAYOUT_GENERAL);
    Barriers::flush(commandBuffer);

    const auto gx = (info.width + 15)/16;
    const auto gy = (info.height + 15)/16;

    SlopeMomentConstants constants {
        .heightScaleX = m_derivedMapHeightScale.x,
        .heightScaleY = m_derivedMapHeightScale.y,
        .dmap_tex_id = m_info.values_tex_id,
        .moments0_image_id = m_slopeMoments0ImageId,
        .moments1_image_id = m_slopeMoments1ImageId
    };

    auto descriptorSet = bindlessDescriptorSet();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("generate_slope_moments"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("generate_slope_moments"), 0, 1, &descriptorSet, 0, 0);
    vkCmdPushConstants(commandBuffer, m_compute.layout("generate_slope_moments"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, gx, gy, 1);

    Barriers::push(moments0.image, subresource, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                   VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Barriers::push(moments1.image, subresource, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                   VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Barriers::flush(commandBuffer);

    textures::generateLOD(commandBuffer, moments0.image, info.width, info.height, levels);
    textures::generateLOD(commandBuffer, moments1.image, info.width, info.height, levels);
}


VulkanDescriptorSetLayout &DisplacementMapGenerator::bindlessDescriptorSetLayout() {
    return const_cast<VulkanDescriptorSetLayout &>(*m_context->bindlessDescriptor->descriptorSetLayout);
}

VkDescriptorSet DisplacementMapGenerator::bindlessDescriptorSet() {
    return m_context->bindlessDescriptor->descriptorSet;
}

BindlessDescriptor &DisplacementMapGenerator::bindlessDescriptor() {
    return *m_context->bindlessDescriptor;
}

void DisplacementMapGenerator::blur(VkCommandBuffer commandBuffer) {
    auto info = displacementMapInfo();
    static struct {
        uint horizontal;
        uint blur_input_index;
        uint blur_output_index;
    } constants {0 ,0, 0};

    static Texture blurInput{};
    static Texture blurOutput{};


    if(blurOutput.format == VK_FORMAT_UNDEFINED || blurOutput.width != info.width || blurOutput.height != info.height) {
        auto format = m_displacementMap.values.format;
        textures::createNoTransition(device(), blurOutput, VK_IMAGE_TYPE_2D, format, {info.width, info.height, 1},
                                     DepthMapAddressMode);
        textures::createNoTransition(device(), blurInput, VK_IMAGE_TYPE_2D, format, {info.width, info.height, 1},
                                     DepthMapAddressMode);

        Barriers::push(blurInput.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

        Barriers::push(blurOutput.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

        Barriers::flush(commandBuffer);
        blurInput.image.currentLayout = VK_IMAGE_LAYOUT_GENERAL;
        blurOutput.image.currentLayout = VK_IMAGE_LAYOUT_GENERAL;
    }

    textures::copy(commandBuffer, m_displacementMap.values, blurInput);

    static auto blur_input_offset = to<uint>(bindlessDescriptor().reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2));
    static auto blur_output_offset = to<uint>(bindlessDescriptor().reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2));

    static std::array<uint, 2> blur_input_index{blur_input_offset, blur_input_offset+1};
    static std::array<uint, 2> blur_output_index{blur_output_offset, blur_output_offset+1};

    bindlessDescriptor().update({ &blurInput, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, blur_input_index[0], VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &blurOutput, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, blur_input_index[1], VK_IMAGE_LAYOUT_GENERAL });

    bindlessDescriptor().update({ &blurOutput, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, blur_output_index[0], VK_IMAGE_LAYOUT_GENERAL });
    bindlessDescriptor().update({ &blurInput, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, blur_output_index[1], VK_IMAGE_LAYOUT_GENERAL });


    int pingPong = 0;
    const auto iterations = ff_options.blurIterations;  // use odd number iterations so blurOut will always be final output
    const auto gx = (info.width + 15)/16;
    const auto gy = (info.height + 15)/16;
    auto descriptorSet = bindlessDescriptorSet();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline("blur"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.layout("blur"), 0, 1, &descriptorSet, 0, 0);

    for(auto i = 0; i < iterations; ++i) {
        constants.horizontal = 1;
        constants.blur_input_index = blur_input_index[pingPong];
        constants.blur_output_index = blur_output_index[pingPong];

        vkCmdPushConstants(commandBuffer, m_compute.layout("blur"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
        vkCmdDispatch(commandBuffer, gx, gy, 1);
        Barrier::computeWriteToRead(commandBuffer);

        constants.horizontal = 0;
        vkCmdPushConstants(commandBuffer, m_compute.layout("blur"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
        vkCmdDispatch(commandBuffer, gx, gy, 1);
        Barrier::computeWriteToRead(commandBuffer);

        pingPong = 1 - pingPong;
    }

    textures::copy(commandBuffer, blurOutput, m_displacementMap.values);

}
