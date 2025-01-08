#pragma once

#include <cinttypes>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstring>

constexpr uint32_t VOL_HEADER_SIZE = 10000;
constexpr const char* VOL_IDENTIFIER = "mdvol";
constexpr const char* VOL_COLOR_FORMAT_GRAYSCALE = "g08";
constexpr const char* VOL_COLOR_FORMAT_INDEXED = "i08";
constexpr const char* VOL_COLOR_FORMAT_RGB = "c24";

struct vol_dim {
    uint32_t x, y, z;
};

struct vol_physical_dim {
    float x, y, z;
};

#pragma pack(push,1)
struct vol_header {
    char identifier[5];
    char type;
    uint32_t length;
    vol_dim dimensions;
    vol_physical_dim physical_dimensions;
    float black_point;
    float white_point;
    float gamma;
    char color_format[3];
    char volume_description[4900];
    char title[151];
    char description[4900];
};

struct vol_format {
    vol_header header;
    std::vector<char> data;
};
#pragma pack(pop)

std::streamsize vol_data_size(const vol_header& header) {
    std::streamsize size = 0;
    if(std::memcmp(header.color_format, VOL_COLOR_FORMAT_GRAYSCALE, 3) == 0
     || std::memcmp(header.color_format, VOL_COLOR_FORMAT_INDEXED, 3) == 0) {
        size = 1;
    }else if(std::memcmp(header.color_format, VOL_COLOR_FORMAT_RGB, 3) == 0) {
        size = 3;
    }

    return size * header.dimensions.x * header.dimensions.y * header.dimensions.z;
}

vol_format vol_load(const std::filesystem::path& path) {
    std::ifstream fin(path.string(), std::ios::binary);
    if(!fin.good()) throw std::runtime_error{"Failed to open file: " + path.string()};

    vol_format format{};

    fin.read(reinterpret_cast<char*>(&format.header), VOL_HEADER_SIZE);

    if(std::memcmp(format.header.identifier, VOL_IDENTIFIER, 5) != 0) {
        throw std::runtime_error{"corrupted vol file"};
    }

    const auto data_size = vol_data_size(format.header);

    fin.read(format.data.data(), data_size);

    return format;

}

