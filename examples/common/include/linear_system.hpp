#pragma once

#include <linalg/linalg.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace linear_system {
    inline constexpr size_t coordinate_index_base = 1;

    struct system {
        nlohmann::json header;
        size_t rows{};
        size_t cols{};
        std::vector<double> A;
        std::vector<double> b;
    };

    inline size_t count_non_zero(std::span<const double> values) {
        size_t count{};

        for (const auto value : values) {
            if (value != 0.0) {
                ++count;
            }
        }

        return count;
    }

    inline void save(const std::filesystem::path& path, const system& input) {
        if (input.A.size() != input.rows * input.cols) {
            throw std::runtime_error{"linear system A size does not match matrix dimensions"};
        }

        if (input.b.size() != input.cols) {
            throw std::runtime_error{"linear system b size does not match matrix column count"};
        }

        std::ofstream out{path};

        if (!out.is_open()) {
            throw std::runtime_error{"unable to open linear system output file: " + path.string()};
        }

        auto header = input.header;
        header["rows"] = input.rows;
        header["cols"] = input.cols;
        header["b_size"] = input.b.size();
        header["matrix_storage"] = "coordinate";
        header["index_base"] = coordinate_index_base;
        header["non_zeros"] = count_non_zero(input.A);
        header["a_values"] = header["non_zeros"];

        out << "# vglib_linear_system 1\n";
        out << "# " << header.dump() << '\n';
        out << "A\n";

        for (size_t r = 0; r < input.rows; ++r) {
            for (size_t c = 0; c < input.cols; ++c) {
                const auto value = input.A[r * input.cols + c];

                if (value != 0.0) {
                    out << (r + coordinate_index_base) << ' '
                        << (c + coordinate_index_base) << ' '
                        << value << '\n';
                }
            }
        }

        out << "b\n";

        for (size_t i = 0; i < input.b.size(); ++i) {
            if (i != 0) {
                out << ' ';
            }

            out << input.b[i];
        }

        out << '\n';
    }

    inline system load(const std::filesystem::path& path) {
        std::ifstream in{path};

        if (!in.is_open()) {
            throw std::runtime_error{"unable to open linear system input file: " + path.string()};
        }

        std::string magic;
        std::getline(in, magic);

        if (magic != "# vglib_linear_system 1") {
            throw std::runtime_error{"invalid linear system file header"};
        }

        std::string header_line;
        std::getline(in, header_line);

        if (!header_line.starts_with("# ")) {
            throw std::runtime_error{"missing linear system metadata header"};
        }

        auto header = nlohmann::json::parse(header_line.substr(2));
        const auto rows = header.at("rows").get<size_t>();
        const auto cols = header.at("cols").get<size_t>();
        const auto b_size = header.at("b_size").get<size_t>();
        const auto matrix_storage = header.value("matrix_storage", std::string{"dense_row_major"});

        std::string section;
        in >> section;

        if (section != "A") {
            throw std::runtime_error{"expected A section in linear system file"};
        }

        std::vector<double> A(rows * cols);

        if (matrix_storage == "coordinate") {
            const auto value_count = header.contains("a_values")
                                         ? header.at("a_values").get<size_t>()
                                         : header.at("non_zeros").get<size_t>();
            const auto index_base = header.value("index_base", coordinate_index_base);

            for (size_t i = 0; i < value_count; ++i) {
                size_t row{};
                size_t col{};
                double value{};
                in >> row >> col >> value;

                if (!in) {
                    throw std::runtime_error{"failed while reading sparse A triplets"};
                }

                if (row < index_base || col < index_base) {
                    throw std::runtime_error{"linear system sparse index is below index base"};
                }

                row -= index_base;
                col -= index_base;

                if (row >= rows || col >= cols) {
                    throw std::runtime_error{"linear system sparse index is out of bounds"};
                }

                A[row * cols + col] = value;
            }
        } else if (matrix_storage == "dense_row_major") {
            for (auto& value : A) {
                in >> value;
            }
        } else {
            throw std::runtime_error{"unsupported linear system matrix storage: " + matrix_storage};
        }

        in >> section;

        if (section != "b") {
            throw std::runtime_error{"expected b section in linear system file"};
        }

        std::vector<double> b(b_size);

        for (auto& value : b) {
            in >> value;
        }

        if (!in) {
            throw std::runtime_error{"failed while reading linear system values"};
        }

        if (b.size() != cols) {
            throw std::runtime_error{"linear system b size must match matrix column count"};
        }

        return {std::move(header), rows, cols, std::move(A), std::move(b)};
    }

    template<typename MatrixType>
        requires linalg::cpu_matrix_type<MatrixType>
    MatrixType create_matrix(const system& input) {
        using value_type = typename MatrixType::value_type;

        if (input.rows != MatrixType::rows || input.cols != MatrixType::cols) {
            throw std::runtime_error{"linear system matrix dimensions do not match MatrixType"};
        }

        auto matrix = linalg::create_matrix<MatrixType>();

        if constexpr (linalg::cpu_dense_matrix_type<MatrixType>) {
            for (size_t r = 0; r < MatrixType::rows; ++r) {
                for (size_t c = 0; c < MatrixType::cols; ++c) {
                    matrix(r, c) = static_cast<value_type>(input.A[r * input.cols + c]);
                }
            }
        } else if constexpr (MatrixType::matrix_layout == linalg::matrix_layout_type::csr) {
            matrix.row_offsets.assign(MatrixType::rows + 1, 0);

            for (size_t r = 0; r < MatrixType::rows; ++r) {
                matrix.row_offsets[r] = static_cast<uint32_t>(matrix.data.size());

                for (size_t c = 0; c < MatrixType::cols; ++c) {
                    const auto value = input.A[r * input.cols + c];

                    if (value != 0.0) {
                        matrix.data.push_back(static_cast<value_type>(value));
                        matrix.col_indices.push_back(static_cast<uint32_t>(c));
                    }
                }
            }

            matrix.row_offsets[MatrixType::rows] = static_cast<uint32_t>(matrix.data.size());
        } else {
            throw std::runtime_error{"linear system loader only supports dense and CSR matrix targets"};
        }

        return matrix;
    }

    template<typename VectorType>
        requires linalg::cpu_dense_vector_type<VectorType>
    VectorType create_vector(const system& input) {
        if (input.b.size() != VectorType::rows) {
            throw std::runtime_error{"linear system vector dimensions do not match VectorType"};
        }

        auto vector = linalg::create_matrix<VectorType>();

        for (size_t i = 0; i < VectorType::rows; ++i) {
            vector(i) = static_cast<typename VectorType::value_type>(input.b[i]);
        }

        return vector;
    }
}
