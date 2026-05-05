#pragma once

#include "common.h"

#include <vector>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <cstdint>


namespace mnist {
    struct Header {
        uint32_t magic{~0u};
        uint32_t num_images{~0u};
        uint32_t rows{~0u};
        uint32_t cols{~0u};
    };

    struct Dataset {
        Header header;
        std::vector<float> images;
        std::vector<int> labels;
    };

    inline uint32_t read_be_uint32(std::ifstream& file) {
        unsigned char bytes[4];
        file.read(reinterpret_cast<char*>(bytes), 4);

        if (!file) {
            throw std::runtime_error("Failed to read 4 bytes from file");
        }

        return (static_cast<uint32_t>(bytes[0]) << 24) |
               (static_cast<uint32_t>(bytes[1]) << 16) |
               (static_cast<uint32_t>(bytes[2]) << 8)  |
               (static_cast<uint32_t>(bytes[3]));
    }

    inline Header load_header(const std::filesystem::path &path) {
        Header header{};

        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Cannot open file: " + path.string());
        }

        header.magic = read_be_uint32(file);
        header.num_images = read_be_uint32(file);
        header.rows = read_be_uint32(file);
        header.cols = read_be_uint32(file);

        return header;
    }

    inline  void load_images(Dataset& dataset, const std::filesystem::path &path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Cannot open file: " + path.string());
        }

        uint32_t magic = read_be_uint32(file);
        uint32_t num_images = read_be_uint32(file);
        uint32_t rows = read_be_uint32(file);
        uint32_t cols = read_be_uint32(file);

        // Validate magic number
        if (magic != 2051) {
            throw std::runtime_error("Invalid MNIST image file (magic != 2051)");
        }

        const size_t image_size = rows * cols;
        const size_t total_size = num_images * image_size;

        std::vector<unsigned char> data(total_size);

        file.read(reinterpret_cast<char*>(data.data()), total_size);

        if (!file) {
            throw std::runtime_error("Failed to read image data");
        }

        dataset.header.magic = magic;
        dataset.header.num_images = num_images;
        dataset.header.rows = rows;
        dataset.header.cols = cols;
        dataset.images = map_range(data, [](auto c){ return static_cast<float>(c/255.f); });
    }

    inline void  load_labels(Dataset& dataset, const std::filesystem::path &path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Cannot open file: " + path.string());
        }

        uint32_t magic = read_be_uint32(file);
        uint32_t num_labels = read_be_uint32(file);

        // Validate magic number
        if (magic != 2049) {
            throw std::runtime_error("Invalid MNIST label file (magic != 2049)");
        }

        std::vector<unsigned char> raw(num_labels);
        file.read(reinterpret_cast<char*>(raw.data()), num_labels);

        if (!file) {
            throw std::runtime_error("Failed to read label data");
        }

        // Convert to int (cleaner for ML usage)
        std::vector<int> labels;
        labels.reserve(num_labels);

        for (unsigned char c : raw) {
            labels.push_back(static_cast<int>(c));
        }

        dataset.labels = labels;
    }

    inline Dataset load(const std::filesystem::path& image_path, const std::filesystem::path& label_path) {
        Dataset dataset{};
        load_images(dataset, image_path);
        load_labels(dataset, label_path);

        return dataset;
    }
}
